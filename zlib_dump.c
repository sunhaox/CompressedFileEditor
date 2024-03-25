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

local int decode_zlib_header(const unsigned char *source, int print_level)
{
    unsigned char cmf, comp_method, comp_info;
    unsigned char flags, check_bits, preset_dictionary, compression_level;
    if (!source)
        return -1;

    cmf = source[0];
    comp_method = cmf & 0xF;
    comp_info = (cmf >> 4) & 0xF;
    print_log_to_both("%s\"ZLIB_HEADER\": {\n",
        print_level_tabel[print_level]);
    print_log_to_both("%s\"COMPRESSION_METHOD\": {\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"bit_size\": 4,\n",
        print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 2], comp_method);
    if (comp_method == 8) {
        print_log_to_both("%s\"description\": \"DEFLATE\"\n",
        print_level_tabel[print_level + 2]);
    } else if (comp_method == 15) {
        print_log_to_both("%s\"description\": \"Reserved\"\n",
            print_level_tabel[print_level + 2]);
        fprintf(stderr, "zlib header decode failed!\n");
        return -1;
    } else {
        print_log_to_both("%s\"description\": \"Invalid\"\n",
            print_level_tabel[print_level + 2]);
        fprintf(stderr, "zlib header decode failed!\n");
        return -1;
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 1]);

    print_log_to_both("%s\"COMPRESSION_INFO\": {\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"bit_size\": 4,\n",
        print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 2], comp_info);
    if (comp_info != 7) {
        fprintf(stderr, "zlib header decode failed!\n");
        return -1;
    }
    print_log_to_both("%s\"description\": \"Window size: %d Bytes\"\n",
        print_level_tabel[print_level + 2], 1 << (comp_info + 8));
    print_log_to_both("%s},\n", print_level_tabel[print_level + 1]);

    flags = source[1];
    check_bits = flags & 0x1F;
    preset_dictionary = (flags >> 5) & 0x1;
    compression_level = (flags >> 6) & 0x3;

    print_log_to_both("%s\"FLAGS\": {\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"FCHECK\": {\n", print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"bit_size\": 5,\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 3], check_bits);
    if ((cmf * 256 + flags) % 31 != 0) {
        print_log_to_both("%s\"description\": \"check failed\"\n",
            print_level_tabel[print_level + 3]);
    } else {
        print_log_to_both("%s\"description\": \"check success\"\n",
            print_level_tabel[print_level + 3]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);

    print_log_to_both("%s\"FDICT\": {\n", print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 3], preset_dictionary);

    if (preset_dictionary) {
        print_log_to_both("%s\"description\": \"dictionary preset\"\n",
            print_level_tabel[print_level + 3]);
    } else {
        print_log_to_both("%s\"description\": \"dictionary not preset\"\n",
            print_level_tabel[print_level + 3]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);

    print_log_to_both("%s\"FLEVEL\": {\n", print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"bit_size\": 2,\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 3], compression_level);

    if (compression_level == 0) {
        print_log_to_both("%s\"description\": \"fastest\"\n",
            print_level_tabel[print_level + 3]);
    } else if (compression_level == 1) {
        print_log_to_both("%s\"description\": \"fast\"\n",
            print_level_tabel[print_level + 3]);
    } else if (compression_level == 2) {
        print_log_to_both("%s\"description\": \"default\"\n",
            print_level_tabel[print_level + 3]);
    } else if (compression_level == 3) {
        print_log_to_both("%s\"description\": \"maximum compression, slowest\"\n",
            print_level_tabel[print_level + 3]);
    }
    print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);
    print_log_to_both("%s}\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);
    return 0;
}

int zlib_dump(unsigned char *dest,
              unsigned long *destlen,
              const unsigned char *source,
              unsigned long sourcelen,
              int print_level)
{
    int ret;
    unsigned zlib_header_size = 2;
    unsigned zlib_footer_szie = 4;
    unsigned source_skip_header_size = sourcelen - zlib_header_size;

    unsigned long decompressed_len = source_skip_header_size;

    print_log_to_both("%s{\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"ZLIB_FORMAT\": {\n", print_level_tabel[print_level + 1]);

    if (decode_zlib_header(source, print_level + 2)) {
        return -1;
    }

    ret = puff(dest, destlen, source + zlib_header_size, &decompressed_len,
        print_level + 2);
    if (dest == NULL) {
        if (ret) {
            fprintf(stderr, "puff() failed with return code %d\n", ret);
            return ret;
        } else {
            fprintf(stderr, "puff() succeeded uncompressing %lu bytes\n", *destlen);
            if (decompressed_len < source_skip_header_size) {
                fprintf(stderr, "%lu compressed bytes unused\n",
                    source_skip_header_size - decompressed_len);
            }
        }
    }

    if (source_skip_header_size - decompressed_len == 4) {
        print_log_to_both("%s\"CHECKSUM_IN_FILE\": {\n", print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"value\": [\n", print_level_tabel[print_level + 3]);
        print_hex_with_buffer((unsigned char *)source + zlib_header_size + decompressed_len, 4, print_level + 4);
        print_log_to_both("%s],\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"description\": \"Adler-32 Checksum in File\"\n", print_level_tabel[print_level + 3]);
        if (dest) {
            print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
            print_log_to_both("%s\"CHECKSUM_CALCULATED\": {\n", print_level_tabel[print_level + 2]);
            print_log_to_both("%s\"value\": [\n", print_level_tabel[print_level + 3]);
            adler32_checksum = swap_uint32(adler32_checksum);
            print_hex_with_buffer((unsigned char *)&adler32_checksum, 4, print_level + 4);
            print_log_to_both("%s],\n", print_level_tabel[print_level + 3]);
            print_log_to_both("%s\"description\": \"Adler-32 Checksum Calculated\"\n", print_level_tabel[print_level + 3]);
            print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);
        } else {
            print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);
        }
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
        strcat (compressed_log_file_name, "zlib_compressed.json");
        strcat (decompressed_log_file_name, "zlib_decompressed.json");
        strcat (decompressed_file_name, "zlib_decompressed.bin");
    }

    compressed_data_log_file = fopen(compressed_log_file_name, "w");

    ret = zlib_dump(NIL, &destlen, source, len, 0);

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
        zlib_dump(dest, &destlen, source, len, 0);
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
