/*
 * puff.c
 * Copyright (C) 2002-2013 Mark Adler
 * For conditions of distribution and use, see copyright notice in puff.h
 * version 2.3, 21 Jan 2013
 *
 * puff.c is a simple inflate written to be an unambiguous way to specify the
 * deflate format.  It is not written for speed but rather simplicity.  As a
 * side benefit, this code might actually be useful when small code is more
 * important than speed, such as bootstrap applications.  For typical deflate
 * data, zlib's inflate() is about four times as fast as puff().  zlib's
 * inflate compiles to around 20K on my machine, whereas puff.c compiles to
 * around 4K on my machine (a PowerPC using GNU cc).  If the faster decode()
 * function here is used, then puff() is only twice as slow as zlib's
 * inflate().
 *
 * All dynamically allocated memory comes from the stack.  The stack required
 * is less than 2K bytes.  This code is compatible with 16-bit int's and
 * assumes that long's are at least 32 bits.  puff.c uses the short data type,
 * assumed to be 16 bits, for arrays in order to conserve memory.  The code
 * works whether integers are stored big endian or little endian.
 *
 * In the comments below are "Format notes" that describe the inflate process
 * and document some of the less obvious aspects of the format.  This source
 * code is meant to supplement RFC 1951, which formally describes the deflate
 * format:
 *
 *    http://www.zlib.org/rfc-deflate.html
 */

/*
 * Change history:
 *
 * 1.0  10 Feb 2002     - First version
 * 1.1  17 Feb 2002     - Clarifications of some comments and notes
 *                      - Update puff() dest and source pointers on negative
 *                        errors to facilitate debugging deflators
 *                      - Remove longest from struct huffman -- not needed
 *                      - Simplify offs[] index in construct()
 *                      - Add input size and checking, using longjmp() to
 *                        maintain easy readability
 *                      - Use short data type for large arrays
 *                      - Use pointers instead of long to specify source and
 *                        destination sizes to avoid arbitrary 4 GB limits
 * 1.2  17 Mar 2002     - Add faster version of decode(), doubles speed (!),
 *                        but leave simple version for readability
 *                      - Make sure invalid distances detected if pointers
 *                        are 16 bits
 *                      - Fix fixed codes table error
 *                      - Provide a scanning mode for determining size of
 *                        uncompressed data
 * 1.3  20 Mar 2002     - Go back to lengths for puff() parameters [Gailly]
 *                      - Add a puff.h file for the interface
 *                      - Add braces in puff() for else do [Gailly]
 *                      - Use indexes instead of pointers for readability
 * 1.4  31 Mar 2002     - Simplify construct() code set check
 *                      - Fix some comments
 *                      - Add FIXLCODES #define
 * 1.5   6 Apr 2002     - Minor comment fixes
 * 1.6   7 Aug 2002     - Minor format changes
 * 1.7   3 Mar 2003     - Added test code for distribution
 *                      - Added zlib-like license
 * 1.8   9 Jan 2004     - Added some comments on no distance codes case
 * 1.9  21 Feb 2008     - Fix bug on 16-bit integer architectures [Pohland]
 *                      - Catch missing end-of-block symbol error
 * 2.0  25 Jul 2008     - Add #define to permit distance too far back
 *                      - Add option in TEST code for puff to write the data
 *                      - Add option in TEST code to skip input bytes
 *                      - Allow TEST code to read from piped stdin
 * 2.1   4 Apr 2010     - Avoid variable initialization for happier compilers
 *                      - Avoid unsigned comparisons for even happier compilers
 * 2.2  25 Apr 2010     - Fix bug in variable initializations [Oberhumer]
 *                      - Add const where appropriate [Oberhumer]
 *                      - Split if's and ?'s for coverage testing
 *                      - Break out test code to separate file
 *                      - Move NIL to puff.h
 *                      - Allow incomplete code only if single code length is 1
 *                      - Add full code coverage test to Makefile
 * 2.3  21 Jan 2013     - Check for invalid code length codes in dynamic blocks
 */

#include <setjmp.h>             /* for setjmp(), longjmp(), and jmp_buf */
#include "puff.h"               /* prototype for puff() */

#define local static            /* for local function definitions */

/*
 * Maximums for allocations and loops.  It is not useful to change these --
 * they are fixed by the deflate format.
 */
#define MAXBITS 15              /* maximum bits in a code */
#define MAXLCODES 286           /* maximum number of literal/length codes */
#define MAXDCODES 30            /* maximum number of distance codes */
#define MAXCODES (MAXLCODES+MAXDCODES)  /* maximum codes lengths to read */
#define FIXLCODES 288           /* number of fixed literal/length codes */

/* input and output state */
struct state {
    /* output state */
    unsigned char *out;         /* output buffer */
    unsigned long outlen;       /* available space at out */
    unsigned long outcnt;       /* bytes written to out so far */

    /* input state */
    const unsigned char *in;    /* input buffer */
    unsigned long inlen;        /* available input at in */
    unsigned long incnt;        /* bytes read so far */
    int bitbuf;                 /* bit buffer */
    int bitcnt;                 /* number of bits in bit buffer */

    /* input limit error return state for bits() and decode() */
    jmp_buf env;
};

/*
 * Return need bits from the input stream.  This always leaves less than
 * eight bits in the buffer.  bits() works properly for need == 0.
 *
 * Format notes:
 *
 * - Bits are stored in bytes from the least significant bit to the most
 *   significant bit.  Therefore bits are dropped from the bottom of the bit
 *   buffer, using shift right, and new bytes are appended to the top of the
 *   bit buffer, using shift left.
 */
local int bits(struct state *s, int need)
{
    long val;           /* bit accumulator (can use up to 20 bits) */

    /* load at least need bits into val */
    val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt == s->inlen)
            longjmp(s->env, 1);         /* out of input */
        val |= (long)(s->in[s->incnt++]) << s->bitcnt;  /* load eight bits */
        s->bitcnt += 8;
    }

    /* drop need bits and update buffer, always zero to seven bits left */
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;

    /* return need bits, zeroing the bits above that */
    return (int)(val & ((1L << need) - 1));
}

local int get_input_bit_position(struct state *s)
{
    // s->incnt: current index of the s->in byte buffer.
    // s->bitcnt: current available bit before s->incnt move to the next.
    return ((s->incnt) << 3) - s->bitcnt;
}

local int get_output_byte_position(struct state *s)
{
    return s->outcnt;
}

/*
 * Process a stored block.
 *
 * Format notes:
 *
 * - After the two-bit stored block type (00), the stored block length and
 *   stored bytes are byte-aligned for fast copying.  Therefore any leftover
 *   bits in the byte that has the last bit of the type, as many as seven, are
 *   discarded.  The value of the discarded bits are not defined and should not
 *   be checked against any expectation.
 *
 * - The second inverted copy of the stored block length does not have to be
 *   checked, but it's probably a good idea to do so anyway.
 *
 * - A stored block can have zero length.  This is sometimes used to byte-align
 *   subsets of the compressed data for random access or partial recovery.
 */
local int stored(struct state *s, int print_level)
{
    unsigned len;       /* length of stored block */
    unsigned nlen;       /* length of one's complement */
    unsigned char data_val;

    print_log_to_both("%s\"BTYPE\": {\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": 2,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"value\": 0,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"description\": \"no compression (aka Stored Block)\"\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    print_log_to_both("%s\"RESERVED\": {\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": %d,\n", print_level_tabel[print_level + 1], s->bitcnt);
    print_log_to_both("%s\"value\": 0,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"description\": \"reserved bits for byte align\"\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    /* discard leftover bits from current byte (assumes s->bitcnt < 8) */
    s->bitbuf = 0;
    s->bitcnt = 0;

    /* get length and check against its one's complement */
    if (s->incnt + 4 > s->inlen) {
        fprintf(stderr, "incomplete stored block!\n");
        return 2;                               /* not enough input */
    }

    len = s->in[s->incnt++];
    len |= s->in[s->incnt++] << 8;

    nlen = s->in[s->incnt++];
    nlen |= s->in[s->incnt++] << 8;

    if (len + nlen != 65535) {
        fprintf(stderr, "len & nlen don't match complement in the stored block!\n");
        return -2;                              /* didn't match complement! */
    }

    /* copy len bytes from in to out */
    if (s->incnt + len > s->inlen) {
        fprintf(stderr, "incomplete stored block!\n");
        return 2;                               /* not enough input */
    }

    print_log_to_both("%s\"LEN\": {\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": 16,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"value\": %d,\n", print_level_tabel[print_level + 1], len);
    print_log_to_both("%s\"description\": \"uncompressed data length (bytes)\"\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    print_log_to_both("%s\"NLEN\": {\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": 16,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"value\": %d,\n", print_level_tabel[print_level + 1], nlen);
    print_log_to_both("%s\"description\": \"complement of LEN (65535 - %d)\"\n",
        print_level_tabel[print_level + 1], len);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    if (print_data_verbose) {
        print_log_to_both("%s\"RAW_DATA\": [\n", print_level_tabel[print_level]);
    }

    while (len--) {
        data_val = s->in[s->incnt++];
        print_compressed_data_hex(data_val, print_level + 1);

        if (s->out != NIL) {
            if (s->outcnt + len > s->outlen) {
                fprintf(stderr, "not enough output space!\n");
                return 1;                      /* not enough output space */
            }
            print_decompressed_data_hex(data_val, print_level + 1);
            adler32(data_val);
            s->out[s->outcnt++] = data_val;
        } else {
            s->outcnt ++;
        }
    }

    print_compressed_data_final(print_level + 1);
    print_decompressed_data_final(print_level + 1);

    if (print_data_verbose) {
        print_log_to_both("%s],\n", print_level_tabel[print_level]);
    }

    /* done with a valid stored block */
    return 0;
}

/*
 * Huffman code decoding tables.  count[1..MAXBITS] is the number of symbols of
 * each length, which for a canonical code are stepped through in order.
 * symbol[] are the symbol values in canonical order, where the number of
 * entries is the sum of the counts in count[].  The decoding process can be
 * seen in the function decode() below.
 */
struct huffman {
    short *count;       /* number of symbols of each length */
    short *symbol;      /* canonically ordered symbols */
};

/*
 * Decode a code from the stream s using huffman table h.  Return the symbol or
 * a negative value if there is an error.  If all of the lengths are zero, i.e.
 * an empty code, or if the code is incomplete and an invalid code is received,
 * then -10 is returned after reading MAXBITS bits.
 *
 * Format notes:
 *
 * - The codes as stored in the compressed data are bit-reversed relative to
 *   a simple integer ordering of codes of the same lengths.  Hence below the
 *   bits are pulled from the compressed data one at a time and used to
 *   build the code value reversed from what is in the stream in order to
 *   permit simple integer comparisons for decoding.  A table-based decoding
 *   scheme (as used in zlib) does not need to do this reversal.
 *
 * - The first code for the shortest length is all zeros.  Subsequent codes of
 *   the same length are simply integer increments of the previous code.  When
 *   moving up a length, a zero bit is appended to the code.  For a complete
 *   code, the last code of the longest length will be all ones.
 *
 * - Incomplete codes are handled by this decoder, since they are permitted
 *   in the deflate format.  See the format notes for fixed() and dynamic().
 */
#ifdef SLOW
local int decode(struct state *s, const struct huffman *h)
{
    int len;            /* current number of bits in code */
    int code;           /* len bits being decoded */
    int first;          /* first code of length len */
    int count;          /* number of codes of length len */
    int index;          /* index of first code of length len in symbol table */

    code = first = index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);             /* get next bit */
        count = h->count[len];
        if (code - count < first)       /* if length len, return symbol */
            return h->symbol[index + (code - first)];
        index += count;                 /* else update for next length */
        first += count;
        first <<= 1;
        code <<= 1;
    }
    fprintf(stderr, "decoder ran out of codes!\n");
    return -10;                         /* ran out of codes */
}

/*
 * A faster version of decode() for real applications of this code.   It's not
 * as readable, but it makes puff() twice as fast.  And it only makes the code
 * a few percent larger.
 */
#else /* !SLOW */
local int decode(struct state *s, const struct huffman *h)
{
    int len;            /* current number of bits in code */
    int code;           /* len bits being decoded */
    int first;          /* first code of length len */
    int count;          /* number of codes of length len */
    int index;          /* index of first code of length len in symbol table */
    int bitbuf;         /* bits from stream */
    int left;           /* bits left in next or left to process */
    short *next;        /* next number of codes */

    bitbuf = s->bitbuf;
    left = s->bitcnt;
    code = first = index = 0;
    len = 1;
    next = h->count + 1;
    while (1) {
        while (left--) {
            code |= bitbuf & 1;
            bitbuf >>= 1;
            count = *next++;
            if (code - count < first) { /* if length len, return symbol */
                s->bitbuf = bitbuf;
                s->bitcnt = (s->bitcnt - len) & 7;
                /*print_to_compressed_log("\t\t\tencoded symbol: %d, decoded symbol: %d\n",
                    code, h->symbol[index + (code - first)]);*/
                return h->symbol[index + (code - first)];
            }
            index += count;             /* else update for next length */
            first += count;
            first <<= 1;
            code <<= 1;
            len++;
        }
        left = (MAXBITS+1) - len;
        if (left == 0)
            break;
        if (s->incnt == s->inlen)
            longjmp(s->env, 1);         /* out of input */
        bitbuf = s->in[s->incnt++];
        if (left > 8)
            left = 8;
    }
    fprintf(stderr, "decoder ran out of codes!\n");
    return -10;                         /* ran out of codes */
}
#endif /* SLOW */


local int get_symbol_index_from_huffman_table(const struct huffman *h, int symbol)
{
    int i;

    for (i = 0; i < MAXCODES; i++) {
        if (h->symbol[i] == symbol)
            return i;
    }

    fprintf(stderr, "symbol %d not exist in huffman table!\n", symbol);
    return -1;
}

local int get_symbol_length_from_huffman_table(const struct huffman *h, int symbol)
{
    int symbol_index, j, len_count = 0;

    symbol_index = get_symbol_index_from_huffman_table(h, symbol);
    if (symbol_index < 0)
        return 0;

    for (j = 1; j <= MAXBITS; j++) {
        len_count += h->count[j];
        if (len_count > symbol_index)
            return j;
    }

    fprintf(stderr, "symbol %d not exist in huffman table!\n", symbol);
    return 0;
}

local int get_encoded_val_from_huffman_table(const struct huffman *h, int symbol)
{
    int symbol_index, symbol_len, i;
    short offs[MAXBITS+2] = {0};
    short next_code[MAXBITS+1] = {0};
    short code = 0, h_count_0;

    symbol_index = get_symbol_index_from_huffman_table(h,symbol);
    if (symbol_index < 0)
        return -1;

    symbol_len = get_symbol_length_from_huffman_table(h, symbol);
    if (symbol_len == 0)
        return -1;

    h_count_0 = h->count[0];
    h->count[0] = 0;

    for (i = 1; i <= MAXBITS; i++) {
        offs[i + 1] = offs[i] + h->count[i];
        code = (code + h->count[i - 1]) << 1;
        next_code[i] = code;
    }

    h->count[0] = h_count_0;

    return next_code[symbol_len] + (symbol_index - offs[symbol_len]);
}

/*
 * Given the list of code lengths length[0..n-1] representing a canonical
 * Huffman code for n symbols, construct the tables required to decode those
 * codes.  Those tables are the number of codes of each length, and the symbols
 * sorted by length, retaining their original order within each length.  The
 * return value is zero for a complete code set, negative for an over-
 * subscribed code set, and positive for an incomplete code set.  The tables
 * can be used if the return value is zero or positive, but they cannot be used
 * if the return value is negative.  If the return value is zero, it is not
 * possible for decode() using that table to return an error--any stream of
 * enough bits will resolve to a symbol.  If the return value is positive, then
 * it is possible for decode() using that table to return an error for received
 * codes past the end of the incomplete lengths.
 *
 * Not used by decode(), but used for error checking, h->count[0] is the number
 * of the n symbols not in the code.  So n - h->count[0] is the number of
 * codes.  This is useful for checking for incomplete codes that have more than
 * one symbol, which is an error in a dynamic block.
 *
 * Assumption: for all i in 0..n-1, 0 <= length[i] <= MAXBITS
 * This is assured by the construction of the length arrays in dynamic() and
 * fixed() and is not verified by construct().
 *
 * Format notes:
 *
 * - Permitted and expected examples of incomplete codes are one of the fixed
 *   codes and any code with a single symbol which in deflate is coded as one
 *   bit instead of zero bits.  See the format notes for fixed() and dynamic().
 *
 * - Within a given code length, the symbols are kept in ascending order for
 *   the code bits definition.
 */
local int construct(struct huffman *h, const short *length, int n, int print_level)
{
    int symbol;         /* current symbol when stepping through length[] */
    int len;            /* current length when stepping through h->count[] */
    int left;           /* number of possible codes left of current length */
    short offs[MAXBITS+1];      /* offsets in symbol table for each length */
    int i,j;
    int encoded_symbol_num;
    int symbol_length_offset;
    int minimal_huffman_code;
    int encoded_huffman_code = 0;
    int print_count = 0;
    char encoded_huffman_code_bit_str[20] = {0};

    /* count number of codes of each length */
    for (len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++)
        (h->count[length[symbol]])++;   /* assumes lengths are within bounds */

    encoded_symbol_num = n - h->count[0];

    if (encoded_symbol_num == 0) {               /* no codes! */
        fprintf(stderr, "construct huffman table error: No symbol encoded!\n");
        return 0;                       /* complete, but decode() will fail */
    } else {
        print_to_compressed_log("%s\"total_symbol_num\": %d,\n",
            print_level_tabel[print_level], n);
        print_to_compressed_log("%s\"encoded_symbol_num\": %d,\n",
            print_level_tabel[print_level], encoded_symbol_num);
        print_to_compressed_log("%s\"not_used_symbol_num\": %d\n",
            print_level_tabel[print_level], h->count[0]);
    }

    /* check for an over-subscribed or incomplete set of lengths */
    left = 1;                           /* one possible code of zero length */
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;                     /* one more bit, double codes left */
        left -= h->count[len];          /* deduct count from possible codes */
        if (left < 0) {
            fprintf(stderr, "over-subscribed lengths for huffman table!\n");
            return left;                /* over-subscribed--return negative */
        }
    }                                   /* left > 0 means incomplete */

    /* generate offsets into symbol table for each length for sorting */
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + h->count[len];

    /*
     * put symbols in table sorted by length, by symbol order within each
     * length
     */
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0)
            h->symbol[offs[length[symbol]]++] = symbol;

    if (print_data_verbose) {
        print_to_compressed_log("%s\"items\": [\n",
            print_level_tabel[print_level]);
    }

    for (i = 0; i < encoded_symbol_num; i++) {
        encoded_huffman_code = get_encoded_val_from_huffman_table(h, h->symbol[i]);

        for (j = 0; j < length[h->symbol[i]]; j++) {
            print_count += sprintf(encoded_huffman_code_bit_str + print_count, "%d",
                (encoded_huffman_code >> (length[h->symbol[i]] -1 - j)) & 0x1);
        }

        if (print_data_verbose) {
            print_to_compressed_log("%s{\n", print_level_tabel[print_level + 1]);
            print_to_compressed_log("%s\"index\": %d,\n",
                print_level_tabel[print_level + 2], i);
            print_to_compressed_log("%s\"symbol_value\": %d,\n",
                print_level_tabel[print_level + 2], h->symbol[i]);
            print_to_compressed_log("%s\"encoded_value\": %d,\n",
                print_level_tabel[print_level + 2], encoded_huffman_code);
            print_to_compressed_log("%s\"encoded_bit_size\": %d,\n",
                print_level_tabel[print_level + 2], length[h->symbol[i]]);
            print_to_compressed_log("%s\"description\": \"symbol %d encoded to %d (b'%s)\"\n",
                print_level_tabel[print_level + 2],
                h->symbol[i], encoded_huffman_code, encoded_huffman_code_bit_str);
            if (i == encoded_symbol_num - 1)
                print_to_compressed_log("%s}\n", print_level_tabel[print_level + 1]);
            else
                print_to_compressed_log("%s},\n", print_level_tabel[print_level + 1]);
        }

        memset(encoded_huffman_code_bit_str, 0, print_count);
        print_count = 0;

    }

    if (print_data_verbose) {
        print_to_compressed_log("%s]\n", print_level_tabel[print_level]);
    }

    /* return zero for complete set, positive for incomplete set */
    return left;
}

/*
 * Decode literal/length and distance codes until an end-of-block code.
 *
 * Format notes:
 *
 * - Compressed data that is after the block type if fixed or after the code
 *   description if dynamic is a combination of literals and length/distance
 *   pairs terminated by and end-of-block code.  Literals are simply Huffman
 *   coded bytes.  A length/distance pair is a coded length followed by a
 *   coded distance to represent a string that occurs earlier in the
 *   uncompressed data that occurs again at the current location.
 *
 * - Literals, lengths, and the end-of-block code are combined into a single
 *   code of up to 286 symbols.  They are 256 literals (0..255), 29 length
 *   symbols (257..285), and the end-of-block symbol (256).
 *
 * - There are 256 possible lengths (3..258), and so 29 symbols are not enough
 *   to represent all of those.  Lengths 3..10 and 258 are in fact represented
 *   by just a length symbol.  Lengths 11..257 are represented as a symbol and
 *   some number of extra bits that are added as an integer to the base length
 *   of the length symbol.  The number of extra bits is determined by the base
 *   length symbol.  These are in the static arrays below, lens[] for the base
 *   lengths and lext[] for the corresponding number of extra bits.
 *
 * - The reason that 258 gets its own symbol is that the longest length is used
 *   often in highly redundant files.  Note that 258 can also be coded as the
 *   base value 227 plus the maximum extra value of 31.  While a good deflate
 *   should never do this, it is not an error, and should be decoded properly.
 *
 * - If a length is decoded, including its extra bits if any, then it is
 *   followed a distance code.  There are up to 30 distance symbols.  Again
 *   there are many more possible distances (1..32768), so extra bits are added
 *   to a base value represented by the symbol.  The distances 1..4 get their
 *   own symbol, but the rest require extra bits.  The base distances and
 *   corresponding number of extra bits are below in the static arrays dist[]
 *   and dext[].
 *
 * - Literal bytes are simply written to the output.  A length/distance pair is
 *   an instruction to copy previously uncompressed bytes to the output.  The
 *   copy is from distance bytes back in the output stream, copying for length
 *   bytes.
 *
 * - Distances pointing before the beginning of the output data are not
 *   permitted.
 *
 * - Overlapped copies, where the length is greater than the distance, are
 *   allowed and common.  For example, a distance of one and a length of 258
 *   simply copies the last byte 258 times.  A distance of four and a length of
 *   twelve copies the last four bytes three times.  A simple forward copy
 *   ignoring whether the length is greater than the distance or not implements
 *   this correctly.  You should not use memcpy() since its behavior is not
 *   defined for overlapped arrays.  You should not use memmove() or bcopy()
 *   since though their behavior -is- defined for overlapping arrays, it is
 *   defined to do the wrong thing in this case.
 */
local int codes(struct state *s,
                const struct huffman *lencode,
                const struct huffman *distcode,
                int print_level)
{
    int symbol;         /* decoded symbol */
    int len;            /* length for copy */
    unsigned dist;      /* distance for copy */
    int len_extra;
    int dist_extra;

    unsigned int leteral_symbol_count = 0;
    unsigned int leteral_symbol_total_bits = 0;

    // max length = 258 (2 bytes)
    unsigned int length_symbol_count = 0;
    unsigned int length_symbol_total_bits = 0;

    // max distance = 32768 (2 bytes)
    unsigned int distance_symbol_count = 0;
    unsigned int distance_symbol_total_bits = 0;

    unsigned int encoded_symbol_total_count = 0;
    unsigned int encoded_stream_total_bits = 0;

    unsigned int decoded_leteral_symbol_total_bits = 0;

    static const short lens[29] = { /* Size base for length codes 257..285 */
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = { /* Extra bits for length codes 257..285 */
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

    static const short dists[30] = { /* Offset base for distance codes 0..29 */
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577};
    static const short dext[30] = { /* Extra bits for distance codes 0..29 */
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
        12, 12, 13, 13};

    if (print_data_verbose) {
        print_to_compressed_log("%s\"ENCODED_BIT_STREAM\": [\n",
            print_level_tabel[print_level]);
        print_to_decompressed_log("%s\"DECOMPRESSED_DATA\": [\n",
            print_level_tabel[print_level]);
    }

    /* decode literals and length/distance pairs */
    do {
        symbol = decode(s, lencode);
        if (symbol < 0)
            return symbol;              /* invalid symbol */

        if (symbol < 256) {             /* literal: symbol is the byte */
            leteral_symbol_count++;
            leteral_symbol_total_bits +=
                get_symbol_length_from_huffman_table(lencode, symbol);
            decoded_leteral_symbol_total_bits += 8;
            print_compressed_data_hex(symbol, print_level + 1);
            /* write out the literal */
            if (s->out != NIL) {
                if (s->outcnt == s->outlen)
                    return 1;
                s->out[s->outcnt] = symbol;
                print_decompressed_data_hex(symbol, print_level + 1);
                adler32(symbol);
            }
            s->outcnt++;
        }
        else if (symbol > 256) {        /* length */
            print_compressed_data_dec(symbol, print_level + 1);
            length_symbol_count++;
            distance_symbol_count++;
            length_symbol_total_bits +=
                get_symbol_length_from_huffman_table(lencode, symbol);
            /* get and compute length */
            symbol -= 257;
            if (symbol >= 29)
                return -10;             /* invalid fixed code */
            len_extra = bits(s, lext[symbol]);
            print_compressed_data_dec(len_extra, print_level + 1);
            len = lens[symbol] + len_extra;
            length_symbol_total_bits += lext[symbol];

            /* get and check distance */
            symbol = decode(s, distcode);
            if (symbol < 0)
                return symbol;          /* invalid symbol */
            print_compressed_data_dec(symbol, print_level + 1);

            distance_symbol_total_bits +=
                get_symbol_length_from_huffman_table(distcode, symbol);

            dist_extra = bits(s, dext[symbol]);
            print_compressed_data_dec(dist_extra, print_level + 1);
            dist = dists[symbol] + dist_extra;
#ifndef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR
            if (dist > s->outcnt) {
                fprintf(stderr, "distance too far back!\n");
                return -11;     /* distance too far back */
            }
#endif

            distance_symbol_total_bits += dext[symbol];
            decoded_leteral_symbol_total_bits += (len << 3);
            /* copy length bytes from distance bytes back */
            if (s->out != NIL) {
                if (s->outcnt + len > s->outlen)
                    return 1;
                while (len--) {
                    s->out[s->outcnt] =
#ifdef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR
                        dist > s->outcnt ?
                            0 :
#endif
                            s->out[s->outcnt - dist];
                    print_decompressed_data_hex(s->out[s->outcnt - dist],
                        print_level + 1);
                    adler32(s->out[s->outcnt - dist]);
                    s->outcnt++;
                }
            }
            else
                s->outcnt += len;
        }
    } while (symbol != 256);            /* end of block symbol */

    leteral_symbol_count++;
    leteral_symbol_total_bits +=
        get_symbol_length_from_huffman_table(lencode, 256);

    print_compressed_data_dec(symbol, print_level + 1);

    print_compressed_data_final(print_level + 1);
    print_decompressed_data_final(print_level + 1);

    if (print_data_verbose) {
        print_to_compressed_log("%s],\n", print_level_tabel[print_level]);
        print_to_decompressed_log("%s],\n", print_level_tabel[print_level]);
    }

    encoded_stream_total_bits = leteral_symbol_total_bits +
        length_symbol_total_bits +
        distance_symbol_total_bits;

    encoded_symbol_total_count = leteral_symbol_count +
        length_symbol_count +
        distance_symbol_count;

    print_to_compressed_log("%s\"leteral_huffman_symbol_count\": %d,\n",
        print_level_tabel[print_level], leteral_symbol_count);
    print_to_compressed_log("%s\"length_huffman_symbol_count\": %d,\n",
        print_level_tabel[print_level], length_symbol_count);
    print_to_compressed_log("%s\"distance_huffman_symbol_count\": %d,\n",
        print_level_tabel[print_level], distance_symbol_count);
    print_to_compressed_log("%s\"encoded_symbol_total_count\": %d,\n",
        print_level_tabel[print_level], encoded_symbol_total_count);
    print_to_compressed_log("%s\"decoded_leteral_total_count\": %d,\n",
        print_level_tabel[print_level], decoded_leteral_symbol_total_bits >> 3);

    print_to_compressed_log("%s\"leteral_huffman_symbol_bits\": %d,\n",
        print_level_tabel[print_level], leteral_symbol_total_bits);
    print_to_compressed_log("%s\"length_symbol_bits\": %d,\n",
        print_level_tabel[print_level], length_symbol_total_bits);
    print_to_compressed_log("%s\"distance_symbol_bits\": %d,\n",
        print_level_tabel[print_level], distance_symbol_total_bits);
    print_to_compressed_log("%s\"encoded_symbol_total_bits\": %d,\n",
        print_level_tabel[print_level], encoded_stream_total_bits);
    print_to_compressed_log("%s\"decoded_leteral_total_bits\": %d,\n",
        print_level_tabel[print_level], decoded_leteral_symbol_total_bits);

    if (encoded_stream_total_bits == 1)
        return 0;

    if (encoded_stream_total_bits != 0) {
        print_to_compressed_log("%s\"compression_ratio\": %f,\n",
            print_level_tabel[print_level],
            (float)decoded_leteral_symbol_total_bits/encoded_stream_total_bits);
    }

    if (decoded_leteral_symbol_total_bits != 0) {
        print_to_compressed_log("%s\"space_saving\": %f,\n",
            print_level_tabel[print_level],
            1 - (float)encoded_stream_total_bits/decoded_leteral_symbol_total_bits);
    }

    /* done with a valid fixed or dynamic block */
    return 0;
}

/*
 * Process a fixed codes block.
 *
 * Format notes:
 *
 * - This block type can be useful for compressing small amounts of data for
 *   which the size of the code descriptions in a dynamic block exceeds the
 *   benefit of custom codes for that block.  For fixed codes, no bits are
 *   spent on code descriptions.  Instead the code lengths for literal/length
 *   codes and distance codes are fixed.  The specific lengths for each symbol
 *   can be seen in the "for" loops below.
 *
 * - The literal/length code is complete, but has two symbols that are invalid
 *   and should result in an error if received.  This cannot be implemented
 *   simply as an incomplete code since those two symbols are in the "middle"
 *   of the code.  They are eight bits long and the longest literal/length\
 *   code is nine bits.  Therefore the code must be constructed with those
 *   symbols, and the invalid symbols must be detected after decoding.
 *
 * - The fixed distance codes also have two invalid symbols that should result
 *   in an error if received.  Since all of the distance codes are the same
 *   length, this can be implemented as an incomplete code.  Then the invalid
 *   codes are detected while decoding.
 */
local int fixed(struct state *s, int print_level)
{
    static int virgin = 1;
    static short lencnt[MAXBITS+1], lensym[FIXLCODES];
    static short distcnt[MAXBITS+1], distsym[MAXDCODES];
    static struct huffman lencode, distcode;
    int decompressed_bytes_size = s->outcnt;
    int bit_position_start = get_input_bit_position(s);
    int bit_position_end = get_input_bit_position(s);
    int ret = 0;

    print_log_to_both("%s\"BTYPE\": {\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": 2,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"value\": 1,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"description\": \"compressed with fixed Huffman codes\"\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    /* build fixed huffman tables if first call (may not be thread safe) */
    if (virgin) {
        int symbol;
        short lengths[FIXLCODES];

        /* construct lencode and distcode */
        lencode.count = lencnt;
        lencode.symbol = lensym;
        distcode.count = distcnt;
        distcode.symbol = distsym;

        /* literal/length table */
        for (symbol = 0; symbol < 144; symbol++)
            lengths[symbol] = 8;
        for (; symbol < 256; symbol++)
            lengths[symbol] = 9;
        for (; symbol < 280; symbol++)
            lengths[symbol] = 7;
        for (; symbol < FIXLCODES; symbol++)
            lengths[symbol] = 8;

        print_to_compressed_log("%s\"extracted_literal_length_huffman_table\": {\n", print_level_tabel[print_level]);
        construct(&lencode, lengths, FIXLCODES, print_level + 1);
        print_to_compressed_log("%s},\n", print_level_tabel[print_level]);
        /* distance table */
        for (symbol = 0; symbol < MAXDCODES; symbol++)
            lengths[symbol] = 5;

        print_to_compressed_log("%s\"extracted_distance_huffman_table\": {\n", print_level_tabel[print_level]);
        construct(&distcode, lengths, MAXDCODES, print_level + 1);
        print_to_compressed_log("%s},\n", print_level_tabel[print_level]);
        /* do this just once */
        virgin = 0;
    }

    /* decode data until end-of-block code */
    ret = codes(s, &lencode, &distcode, print_level);
    bit_position_end = get_input_bit_position(s);
    decompressed_bytes_size = s->outcnt - decompressed_bytes_size;
    print_to_decompressed_log("%s\"DECOMPRESSED_BYTES\": %d,\n",
        print_level_tabel[print_level],
        decompressed_bytes_size);

    return ret;
}

/*
 * Process a dynamic codes block.
 *
 * Format notes:
 *
 * - A dynamic block starts with a description of the literal/length and
 *   distance codes for that block.  New dynamic blocks allow the compressor to
 *   rapidly adapt to changing data with new codes optimized for that data.
 *
 * - The codes used by the deflate format are "canonical", which means that
 *   the actual bits of the codes are generated in an unambiguous way simply
 *   from the number of bits in each code.  Therefore the code descriptions
 *   are simply a list of code lengths for each symbol.
 *
 * - The code lengths are stored in order for the symbols, so lengths are
 *   provided for each of the literal/length symbols, and for each of the
 *   distance symbols.
 *
 * - If a symbol is not used in the block, this is represented by a zero as the
 *   code length.  This does not mean a zero-length code, but rather that no
 *   code should be created for this symbol.  There is no way in the deflate
 *   format to represent a zero-length code.
 *
 * - The maximum number of bits in a code is 15, so the possible lengths for
 *   any code are 1..15.
 *
 * - The fact that a length of zero is not permitted for a code has an
 *   interesting consequence.  Normally if only one symbol is used for a given
 *   code, then in fact that code could be represented with zero bits.  However
 *   in deflate, that code has to be at least one bit.  So for example, if
 *   only a single distance base symbol appears in a block, then it will be
 *   represented by a single code of length one, in particular one 0 bit.  This
 *   is an incomplete code, since if a 1 bit is received, it has no meaning,
 *   and should result in an error.  So incomplete distance codes of one symbol
 *   should be permitted, and the receipt of invalid codes should be handled.
 *
 * - It is also possible to have a single literal/length code, but that code
 *   must be the end-of-block code, since every dynamic block has one.  This
 *   is not the most efficient way to create an empty block (an empty fixed
 *   block is fewer bits), but it is allowed by the format.  So incomplete
 *   literal/length codes of one symbol should also be permitted.
 *
 * - If there are only literal codes and no lengths, then there are no distance
 *   codes.  This is represented by one distance code with zero bits.
 *
 * - The list of up to 286 length/literal lengths and up to 30 distance lengths
 *   are themselves compressed using Huffman codes and run-length encoding.  In
 *   the list of code lengths, a 0 symbol means no code, a 1..15 symbol means
 *   that length, and the symbols 16, 17, and 18 are run-length instructions.
 *   Each of 16, 17, and 18 are followed by extra bits to define the length of
 *   the run.  16 copies the last length 3 to 6 times.  17 represents 3 to 10
 *   zero lengths, and 18 represents 11 to 138 zero lengths.  Unused symbols
 *   are common, hence the special coding for zero lengths.
 *
 * - The symbols for 0..18 are Huffman coded, and so that code must be
 *   described first.  This is simply a sequence of up to 19 three-bit values
 *   representing no code (0) or the code length for that symbol (1..7).
 *
 * - A dynamic block starts with three fixed-size counts from which is computed
 *   the number of literal/length code lengths, the number of distance code
 *   lengths, and the number of code length code lengths (ok, you come up with
 *   a better name!) in the code descriptions.  For the literal/length and
 *   distance codes, lengths after those provided are considered zero, i.e. no
 *   code.  The code length code lengths are received in a permuted order (see
 *   the order[] array below) to make a short code length code length list more
 *   likely.  As it turns out, very short and very long codes are less likely
 *   to be seen in a dynamic code description, hence what may appear initially
 *   to be a peculiar ordering.
 *
 * - Given the number of literal/length code lengths (nlen) and distance code
 *   lengths (ndist), then they are treated as one long list of nlen + ndist
 *   code lengths.  Therefore run-length coding can and often does cross the
 *   boundary between the two sets of lengths.
 *
 * - So to summarize, the code description at the start of a dynamic block is
 *   three counts for the number of code lengths for the literal/length codes,
 *   the distance codes, and the code length codes.  This is followed by the
 *   code length code lengths, three bits each.  This is used to construct the
 *   code length code which is used to read the remainder of the lengths.  Then
 *   the literal/length code lengths and distance lengths are read as a single
 *   set of lengths using the code length codes.  Codes are constructed from
 *   the resulting two sets of lengths, and then finally you can start
 *   decoding actual compressed data in the block.
 *
 * - For reference, a "typical" size for the code description in a dynamic
 *   block is around 80 bytes.
 */
local int dynamic(struct state *s, int print_level)
{
    int nlen, ndist, ncode;             /* number of lengths in descriptor */
    int index, i;                          /* index of lengths[] */
    int symbol_size, symbol_value = 0;
    int err;                            /* construct() return value */
    short lengths[MAXCODES];            /* descriptor code lengths */
    short lencnt[MAXBITS+1], lensym[MAXLCODES];         /* lencode memory */
    short distcnt[MAXBITS+1], distsym[MAXDCODES];       /* distcode memory */
    struct huffman lencode, distcode;   /* length and distance codes */
    static const short order[19] =      /* permutation of code length codes */
        {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int decompressed_bytes_size = s->outcnt;
    int bit_position_start = get_input_bit_position(s);
    int bit_position_end = get_input_bit_position(s);
    int ret = 0;

    /* construct lencode and distcode */
    lencode.count = lencnt;
    lencode.symbol = lensym;
    distcode.count = distcnt;
    distcode.symbol = distsym;

    /* get number of lengths in each table, check lengths */
    nlen = bits(s, 5) + 257;
    ndist = bits(s, 5) + 1;
    ncode = bits(s, 4) + 4;

    if (nlen > MAXLCODES || ndist > MAXDCODES)
        return -3;                      /* bad counts */

    print_log_to_both("%s\"BTYPE\": {\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": 2,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"value\": 2,\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"description\": \"compressed with dynamic Huffman codes\"\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    print_log_to_both("%s\"HLIT\": {\n",
        print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": 5,\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 1], nlen - 257);
    print_log_to_both("%s\"decoded_value\": %d,\n",
        print_level_tabel[print_level + 1], nlen);
    print_log_to_both("%s\"description\": \"%d (%d + 257) of Literal/Length codes encoded\"\n",
        print_level_tabel[print_level + 1], nlen, nlen - 257);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    print_log_to_both("%s\"HDIST\": {\n",
        print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": 5,\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 1], ndist - 1);
    print_log_to_both("%s\"decoded_value\": %d,\n",
        print_level_tabel[print_level + 1], ndist);
    print_log_to_both("%s\"description\": \"%d (%d + 1) of Distance codes encoded\"\n",
        print_level_tabel[print_level + 1], ndist, ndist - 1);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    print_log_to_both("%s\"HCLEN\": {\n",
        print_level_tabel[print_level]);
    print_log_to_both("%s\"bit_size\": 4,\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 1], ncode - 4);
    print_log_to_both("%s\"decoded_value\": %d,\n",
        print_level_tabel[print_level + 1], ncode);
    print_log_to_both("%s\"description\": \"%d (%d + 4) of Code Length codes stored in CODE_LENGTH_TABLE\"\n",
        print_level_tabel[print_level + 1], ncode, ncode - 4);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    if (print_data_verbose) {
        print_to_compressed_log("%s\"CODE_LENGTH_TABLE\": [\n", print_level_tabel[print_level]);
    }

    /* read code length code lengths (really), missing lengths are zero */
    for (index = 0; index < ncode; index++) {
        lengths[order[index]] = bits(s, 3);
        if (print_data_verbose) {
            print_to_compressed_log("%s{\n", print_level_tabel[print_level + 1]);
            print_to_compressed_log("%s\"index\": %d,\n",
                print_level_tabel[print_level + 2], index);
            print_to_compressed_log("%s\"length\": %d,\n",
                print_level_tabel[print_level + 2], order[index]);
            print_to_compressed_log("%s\"bit_size\": 3,\n",
                print_level_tabel[print_level + 2]);
            print_to_compressed_log("%s\"value\": %d,\n",
                print_level_tabel[print_level + 2], lengths[order[index]]);
            print_to_compressed_log("%s\"stored\": 1,\n", print_level_tabel[print_level + 2]);
            if (lengths[order[index]] == 0) {
                print_to_compressed_log("%s\"description\": \"code length stored but not used\"\n",
                    print_level_tabel[print_level + 2]);

            } else {
                print_to_compressed_log("%s\"description\": \"code length %d encoded to %d bits\"\n",
                    print_level_tabel[print_level + 2], order[index], lengths[order[index]]);
            }

            if (index == 18) {
                print_to_compressed_log("%s}\n", print_level_tabel[print_level + 1]);
            } else {
                print_to_compressed_log("%s},\n", print_level_tabel[print_level + 1]);
            }
        }
    }
    for (; index < 19; index++) {
        lengths[order[index]] = 0;
        if (print_data_verbose) {
            print_to_compressed_log("%s{\n", print_level_tabel[print_level + 1]);
            print_to_compressed_log("%s\"index\": %d,\n", print_level_tabel[print_level + 2], index);
            print_to_compressed_log("%s\"length\": %d,\n", print_level_tabel[print_level + 2], order[index]);
            print_to_compressed_log("%s\"bit_size\": 3,\n", print_level_tabel[print_level + 2]);
            print_to_compressed_log("%s\"value\": %d,\n", print_level_tabel[print_level + 2], lengths[order[index]]);
            print_to_compressed_log("%s\"stored\": 0,\n", print_level_tabel[print_level + 2]);
            print_to_compressed_log("%s\"description\": \"code length not used\"\n", print_level_tabel[print_level + 2]);
            if (index == 18) {
                print_to_compressed_log("%s}\n", print_level_tabel[print_level + 1]);
            } else {
                print_to_compressed_log("%s},\n", print_level_tabel[print_level + 1]);
            }
        }
    }

    if (print_data_verbose) {
        print_to_compressed_log("%s],\n", print_level_tabel[print_level]);
    }

    print_log_to_both("%s\"code_length_table_bits\": %d,\n", print_level_tabel[print_level], ncode * 3);

    print_to_compressed_log("%s\"extracted_code_length_huffman_table\": {\n", print_level_tabel[print_level]);

    /* build huffman table for code lengths codes (use lencode temporarily) */
    err = construct(&lencode, lengths, 19, print_level + 1);
    if (err != 0) {               /* require complete code set here */
        fprintf(stderr, "code lengths codes incomplete!\n");
        return -4;
    }

    print_to_compressed_log("%s},\n", print_level_tabel[print_level]);

    if (print_data_verbose) {
        print_to_compressed_log("%s\"LITERAL_LENGTH_DISTANCE_TABLE\": [\n", print_level_tabel[print_level]);
    }
    bit_position_start = get_input_bit_position(s);

    /* read length/literal and distance code length tables */
    index = 0;
    while (index < nlen + ndist) {
        int symbol;             /* decoded value */
        int repeat_times;
        int len;                /* last length to repeat */
        if (print_data_verbose) {
            symbol_size = get_input_bit_position(s);
        }
        symbol = decode(s, &lencode);
        if (print_data_verbose) {
            symbol_size = get_input_bit_position(s) - symbol_size;
            symbol_value = get_encoded_val_from_huffman_table(&lencode, symbol);
        }
        if (symbol < 0)
            return symbol;          /* invalid symbol */

        if (print_data_verbose) {
            print_to_compressed_log("%s{\n",
                print_level_tabel[print_level + 1]);
            print_to_compressed_log("%s\"symbol\": %d,\n",
                print_level_tabel[print_level + 2], index);
            print_to_compressed_log("%s\"bit_size\": %d,\n",
                print_level_tabel[print_level + 2], symbol_size);
            print_to_compressed_log("%s\"value\": %d,\n",
                print_level_tabel[print_level + 2], symbol_value);
            print_to_compressed_log("%s\"decoded_value\": %d,\n",
                print_level_tabel[print_level + 2], symbol);
        }

        if (symbol < 16) {               /* length in 0..15 */
            if (print_data_verbose) {
                if (index < nlen)
                    print_to_compressed_log("%s\"description\": \"literal_length symbol %d encoded to %d bits\"\n",
                        print_level_tabel[print_level + 2], index, symbol);
                else {
                    print_to_compressed_log("%s\"description\": \"distance symbol %d encoded to %d bits\"\n",
                        print_level_tabel[print_level + 2], index - nlen, symbol);
                }
            }
            lengths[index++] = symbol;
        }
        else {                          /* repeat instruction */
            len = 0;                    /* assume repeating zeros */

            if (symbol == 16) {         /* repeat last length 3..6 times */
                if (index == 0) {
                    fprintf(stderr, "repeat lengths with no first length!\n");
                    return -5;          /* no last length! */
                }
                len = lengths[index - 1];       /* last length */
                repeat_times = 3 + bits(s, 2);
                if (print_data_verbose) {
                    print_to_compressed_log("%s\"extra\": {\n", print_level_tabel[print_level + 2]);
                    print_to_compressed_log("%s\"bit_size\": %d,\n", print_level_tabel[print_level + 3], 2);
                    print_to_compressed_log("%s\"value\": %d,\n", print_level_tabel[print_level + 3], repeat_times - 3);
                    print_to_compressed_log("%s\"description\": \"repeat times %d (%d + 3)\"\n", print_level_tabel[print_level + 3], repeat_times, repeat_times - 3);
                    print_to_compressed_log("%s},\n", print_level_tabel[print_level + 2]);

                    if (index <= nlen)
                        print_to_compressed_log("%s\"description\": \"literal_length symbol %d length code %d (repeat previous length code: %d for %d times)\"\n",
                            print_level_tabel[print_level + 2], index, symbol, len, repeat_times);
                    else {
                        print_to_compressed_log("%s\"description\": \"distance symbol %d length code %d (repeat previous length code: %d for %d times)\"\n",
                            print_level_tabel[print_level + 2], index - nlen, symbol, len, repeat_times);
                    }
                }
            }
            else if (symbol == 17){      /* repeat zero 3..10 times */
                repeat_times = 3 + bits(s, 3);
                if (print_data_verbose) {
                    print_to_compressed_log("%s\"extra\": {\n", print_level_tabel[print_level + 2]);
                    print_to_compressed_log("%s\"bit_size\": %d,\n", print_level_tabel[print_level + 3], 3);
                    print_to_compressed_log("%s\"value\": %d,\n", print_level_tabel[print_level + 3], repeat_times - 3);
                    print_to_compressed_log("%s\"description\": \"repeat times %d (%d + 3)\"\n", print_level_tabel[print_level + 3], repeat_times, repeat_times - 3);
                    print_to_compressed_log("%s},\n", print_level_tabel[print_level + 2]);

                    if (index <= nlen)
                        print_to_compressed_log("%s\"description\": \"literal_length symbol %d length code %d (repeat length code 0 for %d times)\"\n",
                            print_level_tabel[print_level + 2], index, symbol, repeat_times);
                    else {
                        print_to_compressed_log("%s\"description\": \"distance symbol %d length code %d (repeat length code 0 for %d times)\"\n",
                            print_level_tabel[print_level + 2], index, symbol, repeat_times);
                    }
                }
            }
            else {                       /* == 18, repeat zero 11..138 times */
                repeat_times = 11 + bits(s, 7);
                if (print_data_verbose) {
                    print_to_compressed_log("%s\"extra\": {\n", print_level_tabel[print_level + 2]);
                    print_to_compressed_log("%s\"bit_size\": %d,\n", print_level_tabel[print_level + 3], 7);
                    print_to_compressed_log("%s\"value\": %d,\n", print_level_tabel[print_level + 3], repeat_times - 11);
                    print_to_compressed_log("%s\"description\": \"repeat times %d (%d + 11)\"\n", print_level_tabel[print_level + 3], repeat_times, repeat_times - 11);
                    print_to_compressed_log("%s},\n", print_level_tabel[print_level + 2]);

                    if (index <= nlen)
                        print_to_compressed_log("%s\"description\": \"literal_length symbol %d length code %d (repeat length code 0 for %d times)\"\n",
                            print_level_tabel[print_level + 2], index, symbol, repeat_times);
                    else {
                        print_to_compressed_log("%s\"description\": \"distance symbol %d length code %d (repeat length code 0 for %d times)\"\n",
                            print_level_tabel[print_level + 2], index, symbol, repeat_times);
                    }
                }
            }

            if (index + repeat_times > nlen + ndist) {
                fprintf(stderr, "repeat more than specified lengths!\n");
                return -6;              /* too many lengths! */
            }
            while (repeat_times--)            /* repeat last or zero symbol times */
                lengths[index++] = len;
        }

        if (print_data_verbose) {
            if (index == nlen + ndist)
                print_to_compressed_log("%s}\n", print_level_tabel[print_level + 1]);
            else
                print_to_compressed_log("%s},\n", print_level_tabel[print_level + 1]);
        }
    }

    bit_position_end = get_input_bit_position(s);

    /* check for end-of-block code -- there better be one! */
    if (lengths[256] == 0) {
        fprintf(stderr, "missing end-of-block code!\n");
        return -9;
    }
    if (print_data_verbose) {
        print_to_compressed_log("%s],\n", print_level_tabel[print_level]);
    }

    print_log_to_both("%s\"literal_length_distance_table_bits\": %d,\n", print_level_tabel[print_level], bit_position_end - bit_position_start);

    print_to_compressed_log("%s\"extracted_literal_length_huffman_table\": {\n", print_level_tabel[print_level]);
    /* build huffman table for literal/length codes */
    err = construct(&lencode, lengths, nlen, print_level + 1);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) {
        fprintf(stderr, "invalid literal/length code lengths!\n");
        return -7;      /* incomplete code ok only for single length 1 code */
    }
    print_to_compressed_log("%s},\n", print_level_tabel[print_level]);

    print_to_compressed_log("%s\"extracted_distance_huffman_table\": {\n", print_level_tabel[print_level]);
    /* build huffman table for distance codes */
    err = construct(&distcode, lengths + nlen, ndist, print_level + 1);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) {
        fprintf(stderr, "invalid distance code lengths!\n");
        return -8;      /* incomplete code ok only for single length 1 code */
    }
    print_to_compressed_log("%s},\n", print_level_tabel[print_level]);

    bit_position_start = get_input_bit_position(s);

    /* decode data until end-of-block code */
    ret = codes(s, &lencode, &distcode, print_level);

    bit_position_end = get_input_bit_position(s);
    decompressed_bytes_size = s->outcnt - decompressed_bytes_size;
    print_to_decompressed_log("%s\"DECOMPRESSED_BYTES\": %d,\n",
        print_level_tabel[print_level], decompressed_bytes_size);

    return ret;
}

/*
 * Inflate source to dest.  On return, destlen and sourcelen are updated to the
 * size of the uncompressed data and the size of the deflate data respectively.
 * On success, the return value of puff() is zero.  If there is an error in the
 * source data, i.e. it is not in the deflate format, then a negative value is
 * returned.  If there is not enough input available or there is not enough
 * output space, then a positive error is returned.  In that case, destlen and
 * sourcelen are not updated to facilitate retrying from the beginning with the
 * provision of more input data or more output space.  In the case of invalid
 * inflate data (a negative error), the dest and source pointers are updated to
 * facilitate the debugging of deflators.
 *
 * puff() also has a mode to determine the size of the uncompressed output with
 * no output written.  For this dest must be (unsigned char *)0.  In this case,
 * the input value of *destlen is ignored, and on return *destlen is set to the
 * size of the uncompressed output.
 *
 * The return codes are:
 *
 *   2:  available inflate data did not terminate
 *   1:  output space exhausted before completing inflate
 *   0:  successful inflate
 *  -1:  invalid block type (type == 3)
 *  -2:  stored block length did not match one's complement
 *  -3:  dynamic block code description: too many length or distance codes
 *  -4:  dynamic block code description: code lengths codes incomplete
 *  -5:  dynamic block code description: repeat lengths with no first length
 *  -6:  dynamic block code description: repeat more than specified lengths
 *  -7:  dynamic block code description: invalid literal/length code lengths
 *  -8:  dynamic block code description: invalid distance code lengths
 *  -9:  dynamic block code description: missing end-of-block code
 * -10:  invalid literal/length or distance code in fixed or dynamic block
 * -11:  distance is too far back in fixed or dynamic block
 *
 * Format notes:
 *
 * - Three bits are read for each block to determine the kind of block and
 *   whether or not it is the last block.  Then the block is decoded and the
 *   process repeated if it was not the last block.
 *
 * - The leftover bits in the last byte of the deflate data after the last
 *   block (if it was a fixed or dynamic block) are undefined and have no
 *   expected values to check.
 */
int puff(unsigned char *dest,           /* pointer to destination pointer */
         unsigned long *destlen,        /* amount of output space */
         const unsigned char *source,   /* pointer to source data pointer */
         unsigned long *sourcelen,
         int print_level)      /* amount of input available */
{
    struct state s;             /* input/output state */
    int last, type;             /* block information */
    int err;                    /* return value */
    int block_index = 0;
    unsigned block_start_bit_position = 0;
    unsigned block_end_bit_position = 0;
    unsigned block_bit_size = 0;
    /* initialize output state */
    s.out = dest;
    s.outlen = *destlen;                /* ignored if dest is NIL */
    s.outcnt = 0;

    /* initialize input state */
    s.in = source;
    s.inlen = *sourcelen;
    s.incnt = 0;
    s.bitbuf = 0;
    s.bitcnt = 0;

    print_log_to_both("%s\"DEFLATE_BLOCK\": [\n", print_level_tabel[print_level]);

    /* return if bits() or decode() tries to read past available input */
    if (setjmp(s.env) != 0) {             /* if came back here via longjmp() */
        err = 2;                        /* then skip do-loop, return error */
        fprintf(stderr, "try to read past available input!\n");
    } else {
        /* process blocks until last block or error */
        do {
            last = bits(&s, 1);         /* one if last block */
            block_index++;

            print_log_to_both("%s{\n", print_level_tabel[print_level + 1]);

            print_log_to_both("%s\"BLOCK_BIT_POSITION\": %d,\n",
                print_level_tabel[print_level + 2],
                block_start_bit_position);

            print_log_to_both("%s\"BFINAL\": {\n",
                print_level_tabel[print_level + 2]);
            print_log_to_both("%s\"bit_size\": 1,\n",
                print_level_tabel[print_level + 3]);
            print_log_to_both("%s\"value\": %d,\n",
                print_level_tabel[print_level + 3], last);
            if (last) {
                print_log_to_both("%s\"description\": \"last block marker = yes\"\n",
                    print_level_tabel[print_level + 3]);
            } else {
                print_log_to_both("%s\"description\": \"last block marker = no\"\n",
                    print_level_tabel[print_level + 3]);
            }
            print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);

            type = bits(&s, 2);         /* block type 0..3 */
            if (type == 0) {
                err = stored(&s, print_level + 2);
            } else if (type == 1) {
                err = fixed(&s, print_level + 2);
            } else if (type == 2) {
                err = dynamic(&s, print_level + 2);
            } else {
                /* type == 3, invalid */
                print_log_to_both("%s\"BTYPE\": {\n",
                    print_level_tabel[print_level + 2]);
                print_log_to_both("%s\"bit_size\": 2,\n", print_level_tabel[print_level + 3]);
                print_log_to_both("%s\"value\": %d,\n", print_level_tabel[print_level + 3], type);
                print_log_to_both("%s\"description\": \"invalid block type (type == 3)\"\n",
                    print_level_tabel[print_level + 3]);
                print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);

                fprintf(stderr, "invalid block type (type == 3)!\n");
                err = -1;
            }

            block_end_bit_position = get_input_bit_position(&s);
            block_bit_size = block_end_bit_position - block_start_bit_position;

            print_log_to_both("%s\"BLOCK_BIT_SIZE\": %d\n",
                print_level_tabel[print_level + 2], block_bit_size);

            block_start_bit_position = get_input_bit_position(&s);
            if (block_start_bit_position == (*sourcelen << 3)) {
                err = 0;
                print_log_to_both("%s}\n", print_level_tabel[print_level + 1]);
                break;
            } else if (last) {
                print_log_to_both("%s}\n", print_level_tabel[print_level + 1]);
            } else {
                print_log_to_both("%s},\n", print_level_tabel[print_level + 1]);
            }

            if (err != 0)
                break;                  /* return with error */
        } while (!last);
    }

    print_log_to_both("%s],\n", print_level_tabel[print_level]);

    if (err == 0) {
        print_log_to_both("%s\"BLOCK_SUMMARY\": {\n",
            print_level_tabel[print_level]);
        print_log_to_both("%s\"block_num\": %d,\n",
            print_level_tabel[print_level + 1], block_index);
        print_log_to_both("%s\"decompressed_bytes\": %d\n",
            print_level_tabel[print_level + 1], s.outcnt);
        print_log_to_both("%s},\n", print_level_tabel[print_level]);
    }

    /* update the lengths and return */
    if (err <= 0) {
        *destlen = s.outcnt;
        *sourcelen = s.incnt;
    }
    return err;
}
