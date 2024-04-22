/*
 * pufftest.c
 * Copyright (C) 2002-2013 Mark Adler
 * For conditions of distribution and use, see copyright notice in puff.h
 * version 2.3, 21 Jan 2013
 */

/* Example of how to use puff().

   Usage: puff [-w] [-f] [-nnn] file
          ... | puff [-w] [-f] [-nnn]

   where file is the input file with deflate data, nnn is the number of bytes
   of input to skip before inflating (e.g. to skip a zlib or gzip header), and
   -w is used to write the decompressed data to stdout.  -f is for coverage
   testing, and causes pufftest to fail with not enough output space (-f does
   a write like -w, so -w is not required). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puff.h"

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

#define local static

unsigned char blockChecksum_g;
unsigned char contentChecksum_g;

/* Return size times approximately the cube root of 2, keeping the result as 1,
   3, or 5 times a power of 2 -- the result is always > size, until the result
   is the maximum value of an unsigned long, where it remains.  This is useful
   to keep reallocations less than ~33% over the actual data. */
local size_t bythirds(size_t size)
{
    int n;
    size_t m;

    m = size;
    for (n = 0; m; n++)
        m >>= 1;
    if (n < 3)
        return size + 1;
    n -= 3;
    m = size >> n;
    m += m == 6 ? 2 : 1;
    m <<= n;
    return m > size ? m : (size_t)(-1);
}

/* Read the input file *name, or stdin if name is NULL, into allocated memory.
   Reallocate to larger buffers until the entire file is read in.  Return a
   pointer to the allocated data, or NULL if there was a memory allocation
   failure.  *len is the number of bytes of data read from the input file (even
   if load() returns NULL).  If the input file was empty or could not be opened
   or read, *len is zero. */
local void *load(const char *name, size_t *len)
{
    size_t size;
    void *buf, *swap;
    FILE *in;

    *len = 0;
    buf = malloc(size = 4096);
    if (buf == NULL)
        return NULL;
    in = name == NULL ? stdin : fopen(name, "rb");
    if (in != NULL) {
        for (;;) {
            *len += fread((char *)buf + *len, 1, size - *len, in);
            if (*len < size) break;
            size = bythirds(size);
            if (size == *len || (swap = realloc(buf, size)) == NULL) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = swap;
        }
        fclose(in);
    }
    return buf;
}

local unsigned decode_lz4_header(const unsigned char *source, int print_level)
{
    unsigned char dictId, c_checksum, c_size, b_checksum, b_indep, version, reserved;
    unsigned char block_max_size;
    unsigned char hc;
    unsigned char c_size_size, d_id_size;
    unsigned char flags;

    if (!source)
        return -1;

    print_log_to_both("%s\"LZ4_HEADER\": {\n",
        print_level_tabel[print_level]);
    print_log_to_both("%s\"MAGIC NUMBER\": {\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"bit_size\": 32,\n",
        print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"value\": [\n",
        print_level_tabel[print_level + 2]);
    print_hex_with_buffer((unsigned char *)source, 4, print_level+3);
    print_log_to_both("%s]\n", print_level_tabel[print_level + 2]);
    print_log_to_both("%s},\n", print_level_tabel[print_level + 1]);

    flags = source[4];
    dictId = flags & 0x1;
    reserved = (flags >> 1) & 0x1;
    c_checksum = (flags >> 2) & 0x1;
    c_size = (flags >> 3) & 0x1;
    b_checksum = (flags >> 4) & 0x1;
    b_indep = (flags >> 5) & 0x1;
    version = (flags >> 6) & 0x3;
    blockChecksum_g = b_checksum;
    contentChecksum_g = c_checksum;
    print_log_to_both("%s\"FRAME DESCRIPTOR\": {\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"FLG\": {\n",
        print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"Dictionary ID flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], dictId);
    if (dictId == 0) {
        print_log_to_both("%s\"description\": \"a 4-bytes Dict-ID field will not be present\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (dictId == 1) {
        print_log_to_both("%s\"description\": \"a 4-bytes Dict-ID field will be present, after the descriptor flags and the Content Size\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"RESERVED\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Content checksum flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], c_checksum);
    if (c_checksum == 0) {
        print_log_to_both("%s\"description\": \"a 32-bits content checksum will not be appended after the EndMark\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (c_checksum == 1) {
        print_log_to_both("%s\"description\": \"a 32-bits content checksum will be appended after the EndMark\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Content Size flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], c_size);
    if (c_size == 0) {
        print_log_to_both("%s\"description\": \"the uncompressed size of data included within the frame will not be present as an 8 bytes unsigned little endian value\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (c_size == 1) {
        print_log_to_both("%s\"description\": \"the uncompressed size of data included within the frame will be present as an 8 bytes unsigned little endian value, after the flags\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Block checksum flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], b_checksum);
    if (b_checksum == 0) {
        print_log_to_both("%s\"description\": \"each data block will not be followed by a 4-bytes checksum\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (b_checksum == 1) {
        print_log_to_both("%s\"description\": \"each data block will be followed by a 4-bytes checksum\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Block Independence flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], b_indep);
    if (b_indep == 1) {
        print_log_to_both("%s\"description\": \"blocks are independent.\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (b_indep == 0) {
        print_log_to_both("%s\"description\": \"each block depends on previous ones(up to LZ4 window size, which is 64 KB).\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Version Number\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 2,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], version);
    print_log_to_both("%s\"description\": \"2 bits filed, must be set to 01.\"\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s}\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);

    flags = source[5];
    block_max_size = (flags >> 4) & 0x7;
    print_log_to_both("%s\"DB\": {\n",
        print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"RSVD0\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 4\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Block MaxSize\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 3,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], block_max_size);
    if (block_max_size == 4) {
        print_log_to_both("%s\"description\": \"64KB\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (block_max_size == 5) {
        print_log_to_both("%s\"description\": \"256KB\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (block_max_size == 6) {
        print_log_to_both("%s\"description\": \"1MB\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (block_max_size == 7) {
        print_log_to_both("%s\"description\": \"4MB\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"RSVD1\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s}\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
    if (c_size == 1) {
        c_size_size = 8;
        print_log_to_both("%s\"Content Size\": {\n",
            print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"bit_size\": 64,\n",
            print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"value\": [\n",
            print_level_tabel[print_level + 3]);
        print_hex_with_buffer((unsigned char *)source + 6, 8, print_level+4);
        print_log_to_both("%s],\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"description\": \"the original (uncompressed) size\"\n",
            print_level_tabel[print_level + 3]);
        print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
    }
    else {
        c_size_size = 0;
    }
    
    if (dictId == 1) {
        d_id_size = 4;
        print_log_to_both("%s\"Dictionary ID\": {\n",
            print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"bit_size\": 32,\n",
            print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"value\": [\n",
            print_level_tabel[print_level + 3]);
        print_hex_with_buffer((unsigned char *)source + c_size_size + 6, 4, print_level+4);
        print_log_to_both("%s]\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
    }
    else {
        d_id_size = 0;
    }

    hc = source[6 + c_size_size + d_id_size];
    print_log_to_both("%s\"Header Checksum\": {\n",
        print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"bit_size\": 8,\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"value\": %d\n",
        print_level_tabel[print_level + 3], hc);
    print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);
    print_log_to_both("%s}\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    return c_size_size + d_id_size + 7;
}

int decode_lz4_block(const unsigned char *source, int print_level)
{
    unsigned int eof, block_size, byte_count;
    unsigned char compressed_flag;
    
    eof = *(unsigned int*)source;

    if (eof == 0) {
        return 0;
    }

    byte_count = 0;

    print_log_to_both("%s\"LZ4_BLOCK\": [\n",
        print_level_tabel[print_level]);
    do
    {
        print_log_to_both("%s{\n", print_level_tabel[print_level + 1]);
        print_log_to_both("%s\"BLOCK_BIT_POSITION\": %d,\n", print_level_tabel[print_level + 2], byte_count * 8);
        block_size = *(unsigned int*)source;
        compressed_flag = 0x1 & (block_size >> 31);
        block_size = (block_size << 1) >> 1;
        print_log_to_both("%s\"BLOCK_BIT_SIZE\": %d,\n", print_level_tabel[print_level + 2], (block_size+4) * 8);
        if (compressed_flag == 1) {
            if (blockChecksum_g == 1) {
                print_log_to_both("%s\"COMPRESSED_FLAG\": \"UNCOMPRESSED\",\n", print_level_tabel[print_level + 2]);
            }
            else {
                print_log_to_both("%s\"COMPRESSED_FLAG\": \"UNCOMPRESSED\"\n", print_level_tabel[print_level + 2]);
            }
        }
        else if (compressed_flag == 0){
            if (blockChecksum_g == 1) {
                print_log_to_both("%s\"COMPRESSED_FLAG\": \"COMPRESSED\",\n", print_level_tabel[print_level + 2]);
            }
            else {
                print_log_to_both("%s\"COMPRESSED_FLAG\": \"COMPRESSED\"\n", print_level_tabel[print_level + 2]);
            }
        }
        // TODO decode
        
        if (blockChecksum_g == 1) {
            print_log_to_both("%s\"block checksum\": [\n", print_level_tabel[print_level + 2]);
            print_hex_with_buffer((unsigned char*)source + 4 + block_size, 4, print_level+3);
            print_log_to_both("%s]\n", print_level_tabel[print_level + 2]);

            byte_count += 4;
            source += 4;
        }
        
        byte_count += block_size + 4;
        source += block_size + 4;
        if(*(unsigned int*) source == 0) {
            print_log_to_both("%s}\n", print_level_tabel[print_level + 1]);
        }
        else {
            print_log_to_both("%s},\n", print_level_tabel[print_level + 1]);
        }
    } while (*(unsigned int*) source != 0);
    print_log_to_both("%s],\n", print_level_tabel[print_level]);

    return byte_count;
}

int lz4_dump(unsigned char *dest,
              unsigned long *destlen,
              const unsigned char *source,
              unsigned long sourcelen,
              int print_level)
{
    int ret;
    unsigned lz4_header_size;
    unsigned lz4_blocks_size;

    print_log_to_both("%s{\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"LZ4_FORMAT\": {\n", print_level_tabel[print_level + 1]);

    lz4_header_size = decode_lz4_header(source, print_level + 2);
    if (lz4_header_size == 0) {
        return -1;
    }

    lz4_blocks_size = decode_lz4_block(source + lz4_header_size, print_level + 2);

    print_log_to_both("%s\"EOF\": {\n", print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"bit_size\": 32,\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"value\": [\n", print_level_tabel[print_level + 3]);
    print_hex_with_buffer((unsigned char *)source + lz4_header_size + lz4_blocks_size, 4, print_level+4);
    print_log_to_both("%s]\n", print_level_tabel[print_level + 3]);
    if (contentChecksum_g)
        print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
    else 
        print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);

    if (contentChecksum_g) {
        print_log_to_both("%s\"Content Checksum\": {\n", print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"bit_size\": 32,\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"value\": [\n", print_level_tabel[print_level + 3]);
        print_hex_with_buffer((unsigned char *)source + lz4_header_size + lz4_blocks_size + 4, 4, print_level+4);
        print_log_to_both("%s]\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);
    }

    print_log_to_both("%s}\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s}\n", print_level_tabel[print_level]);

    return ret;
}

int main(int argc, char **argv)
{
    int ret, put = 0, i, wr_file = 0;
    unsigned file_name_len = 0;
    char *arg, *name = NULL;
    unsigned char *source = NULL, *dest;
    size_t len = 0;
    unsigned long destlen;
    char compressed_log_file_name[250] = {0};
    char decompressed_log_file_name[250] = {0};
    char decompressed_file_name[250] = {0};

    /* process arguments */
    while (arg = *++argv, --argc)
        if (arg[0] == '-') {
            if (arg[1] == 'w' && arg[2] == 0)
                put = 1, wr_file = 1;
            else if (arg[1] == 'v' && arg[2] == 0)
                put = 1, print_data_verbose = 1;
            else {
                fprintf(stderr, "invalid option %s\n", arg);
                return 3;
            }
        }
        else if (name != NULL) {
            fprintf(stderr, "only one file name allowed\n");
            return 3;
        }
        else
            name = arg;
    source = load(name, &len);
    if (source == NULL) {
        fprintf(stderr, "memory allocation failure\n");
        return 4;
    }
    if (len == 0) {
        fprintf(stderr, "could not read %s, or it was empty\n",
                name == NULL ? "<stdin>" : name);
        free(source);
        return 3;
    }

    file_name_len = strlen(name);
    if (file_name_len < 200) {
        strcpy(compressed_log_file_name, name);
        strcpy(decompressed_log_file_name, name);
        strcpy(decompressed_file_name, name);
        strcat (compressed_log_file_name, "_compressed.json");
        strcat (decompressed_log_file_name, "_decompressed.json");
        strcat (decompressed_file_name, "_decompressed.bin");
    } else {
        strcat (compressed_log_file_name, "lz4_compressed.json");
        strcat (decompressed_log_file_name, "lz4_decompressed.json");
        strcat (decompressed_file_name, "lz4_decompressed.bin");
    }

    compressed_data_log_file = fopen(compressed_log_file_name, "w");

    ret = lz4_dump(NIL, &destlen, source, len, 0);

    fclose(compressed_data_log_file);
    compressed_data_log_file = NULL;

    /* if requested, inflate again and write decompressed data to stdout */
    if (put && ret == 0) {
        dest = malloc(destlen);
        if (dest == NULL) {
            fprintf(stderr, "memory allocation failure\n");
            return 4;
        }

        decompressed_data_log_file = fopen(decompressed_log_file_name, "w");
        lz4_dump(dest, &destlen, source, len, 0);
        fclose(decompressed_data_log_file);

        if (wr_file) {
            decompressed_data_file = fopen(decompressed_file_name, "wb");
            fwrite(dest, 1, destlen, decompressed_data_file);
            fclose(decompressed_data_file);
        }

        free(dest);
    }

    /* clean up */
    free(source);
    return ret;
}
