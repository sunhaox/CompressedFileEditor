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
#include "zstd_decompress.h"

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

#define local static

unsigned char contentChecksum_g;

typedef struct {
    u8* address;
    size_t size;
} buffer_s;

static void freeBuffer(buffer_s b) { free(b.address); }

static buffer_s read_file(const char *path)
{
    FILE* const f = fopen(path, "rb");
    if (!f) ERR_OUT("failed to open file %s \n", path);

    fseek(f, 0L, SEEK_END);
    size_t const size = (size_t)ftell(f);
    rewind(f);

    void* const ptr = malloc(size);
    if (!ptr) ERR_OUT("failed to allocate memory to hold %s \n", path);

    size_t const read = fread(ptr, 1, size, f);
    if (read != size) ERR_OUT("error while reading file %s \n", path);

    fclose(f);
    buffer_s const b = { ptr, size };
    return b;
}


local unsigned decode_zstd_header(const unsigned char *source, int print_level)
{
    unsigned char dic_id_flag, c_checksum_flag, unused_bit, single_segment_flag, frame_content_size_flag;
    unsigned char mantissa, exponent, window_log, window_base, window_add, window_size;
    unsigned int dic_id;
    unsigned long frame_content_size, fcs_value;
    unsigned char window_des_size, dic_id_size, frame_content_size_size;
    unsigned char flags;

    if (!source)
        return -1;

    print_log_to_both("%s\"ZSTD_HEADER\": {\n",
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
    dic_id_flag = flags & 0x3;
    c_checksum_flag = (flags >> 2) & 0x1;
    unused_bit = (flags >> 4) & 0x1;
    single_segment_flag = (flags >> 5) & 0x1;
    frame_content_size_flag = (flags >> 6) & 0x3;
    contentChecksum_g = c_checksum_flag;
    print_log_to_both("%s\"FRAME HEADER\": {\n",
        print_level_tabel[print_level + 1]);
    print_log_to_both("%s\"Frame Header Descriptor\": {\n",
        print_level_tabel[print_level + 2]);
    print_log_to_both("%s\"Dictionary ID flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 2,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], dic_id_flag);
    if (dic_id_flag == 0) {
        dic_id_size = 0;
        print_log_to_both("%s\"description\": \"DID_Field_Size = 0\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (dic_id_flag == 1) {
        dic_id_size = 1;
        print_log_to_both("%s\"description\": \"DID_Field_Size = 1\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (dic_id_flag == 2) {
        dic_id_size = 2;
        print_log_to_both("%s\"description\": \"DID_Field_Size = 2\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (dic_id_flag == 3) {
        dic_id_size = 4;
        print_log_to_both("%s\"description\": \"DID_Field_Size = 4\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Content Checksum Flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d\n",
        print_level_tabel[print_level + 4], c_checksum_flag);
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"RESERVED\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Unused bit\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d\n",
        print_level_tabel[print_level + 4], unused_bit);
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Single Segment Flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 1,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], single_segment_flag);
    if (single_segment_flag == 0) {
        print_log_to_both("%s\"description\": \"data don't need be regenerated within a single continuous memory segment\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (single_segment_flag == 1) {
        print_log_to_both("%s\"description\": \"data must be regenerated within a single continuous memory segment\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"Frame Content Size Flag\": {\n",
        print_level_tabel[print_level + 3]);
    print_log_to_both("%s\"bit_size\": 2,\n",
        print_level_tabel[print_level + 4]);
    print_log_to_both("%s\"value\": %d,\n",
        print_level_tabel[print_level + 4], frame_content_size_flag);
    if (frame_content_size_flag == 0) {
        if (single_segment_flag == 1) {
            frame_content_size_size = 1;
            print_log_to_both("%s\"description\": \"FCS_Field_Size = 1\"\n",
                print_level_tabel[print_level + 4]);
        }
        else {
            frame_content_size_size = 0;
            print_log_to_both("%s\"description\": \"FCS_Field_Size = 0\"\n",
                print_level_tabel[print_level + 4]);
        }
    }
    else if (frame_content_size_flag == 1) {
        frame_content_size_size = 2;
        print_log_to_both("%s\"description\": \"FCS_Field_Size = 2\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (frame_content_size_flag == 2) {
        frame_content_size_size = 4;
        print_log_to_both("%s\"description\": \"FCS_Field_Size = 4\"\n",
            print_level_tabel[print_level + 4]);
    }
    else if (frame_content_size_flag == 3) {
        frame_content_size_size = 8;
        print_log_to_both("%s\"description\": \"FCS_Field_Size = 8\"\n",
            print_level_tabel[print_level + 4]);
    }
    print_log_to_both("%s}\n", print_level_tabel[print_level + 3]);
    if (single_segment_flag == 0 || dic_id_size > 0 || frame_content_size_size > 0)
        print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
    else
        print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);

    if (single_segment_flag == 0) {
        window_des_size = 1;
        
        flags = source[5];
        mantissa = flags & 0x7;
        exponent = (flags >> 3);
        window_log = 10 + exponent;
        window_base = 1 << window_log;
        window_add = (window_base / 8) * mantissa;
        window_size = window_base + window_add;
        print_log_to_both("%s\"Window Descriptor\": {\n",
            print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"Mantissa\": {\n",
            print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"bit_size\": 3,\n",
            print_level_tabel[print_level + 4]);
        print_log_to_both("%s\"value\": %d\n",
            print_level_tabel[print_level + 4], mantissa);
        print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"Exponent\": {\n",
            print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"bit_size\": 5,\n",
            print_level_tabel[print_level + 4]);
        print_log_to_both("%s\"value\": %d\n",
            print_level_tabel[print_level + 4], exponent);
        print_log_to_both("%s},\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"description\": \"window size = %d\"\n",
            print_level_tabel[print_level + 3], window_size);
        if (dic_id_size > 0 || frame_content_size_size > 0)
            print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
        else
            print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);
    }
    else {
        window_des_size = 0;
    }

    if (dic_id_size > 0) {
        if (dic_id_size == 1) {
            dic_id = source[5 + window_des_size];
        }
        else if (dic_id_size == 2)
        {
            dic_id = *(unsigned short *)(source + 5 + window_des_size);
        }
        else if (dic_id_size == 4)
        {
            dic_id = *(unsigned int *)(source + 5 + window_des_size);
        }
        print_log_to_both("%s\"Dictionary ID\": {\n",
            print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"bit_size\": %d,\n",
            print_level_tabel[print_level + 3], dic_id_size * 8);
        print_log_to_both("%s\"value\": %d\n",
            print_level_tabel[print_level + 3], dic_id);
        if (frame_content_size_size > 0)
            print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
        else
            print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);
    }

    if (frame_content_size_size > 0) {
        print_log_to_both("%s\"Frame Content Size\": {\n",
            print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"bit_size\": %d,\n",
            print_level_tabel[print_level + 3], frame_content_size_size * 8);
        if (frame_content_size_size == 1) {
            fcs_value = source[5 + window_des_size + dic_id_size];
            frame_content_size = fcs_value;
            print_log_to_both("%s\"value\": %d,\n",
                print_level_tabel[print_level + 3], fcs_value);
            print_log_to_both("%s\"description\": \"The original (uncompressed) size is %d\"\n",
                print_level_tabel[print_level + 3], frame_content_size);
        }
        else if (frame_content_size_size == 2)
        {
            fcs_value = *(unsigned short *)(source + 5 + window_des_size + dic_id_size);
            frame_content_size = fcs_value + 256;
            print_log_to_both("%s\"value\": %d,\n",
                print_level_tabel[print_level + 3], fcs_value);
            print_log_to_both("%s\"description\": \"The original (uncompressed) size is (256+%d)=%d\"\n",
                print_level_tabel[print_level + 3], fcs_value, frame_content_size);
        }
        else if (frame_content_size_size == 4)
        {
            fcs_value = *(unsigned int *)(source + 5 + window_des_size + dic_id_size);
            frame_content_size = fcs_value;
            print_log_to_both("%s\"value\": %d,\n",
                print_level_tabel[print_level + 3], fcs_value);
            print_log_to_both("%s\"description\": \"The original (uncompressed) size is %d\"\n",
                print_level_tabel[print_level + 3], frame_content_size);
        }
        else if (frame_content_size_size == 8)
        {
            fcs_value = *(unsigned long *)(source + 5 + window_des_size + dic_id_size);
            frame_content_size = fcs_value;
            print_log_to_both("%s\"value\": %d,\n",
                print_level_tabel[print_level + 3], fcs_value);
            print_log_to_both("%s\"description\": \"The original (uncompressed) size is %d\"\n",
                print_level_tabel[print_level + 3], frame_content_size);
        }
        print_log_to_both("%s}\n", print_level_tabel[print_level + 2]);
    }

    print_log_to_both("%s}\n", print_level_tabel[print_level + 1]);
    print_log_to_both("%s},\n", print_level_tabel[print_level]);

    return window_des_size + dic_id_size + frame_content_size_size + 5;
}

int zstd_dump(void *const dst, const size_t dst_len,
              const void *const src, const size_t src_len,
              dictionary_t* parsed_dict,
              int print_level)
{
    int ret = 0;
    unsigned zstd_header_size;
    unsigned zstd_blocks_size;

    print_log_to_both("%s{\n", print_level_tabel[print_level]);
    print_log_to_both("%s\"ZSTD_FORMAT\": {\n", print_level_tabel[print_level + 1]);

    zstd_header_size = decode_zstd_header(src, print_level + 2);
    if (zstd_header_size == 0) {
        return -1;
    }

    size_t const src_offset =
        ZSTD_decompress_with_dict(dst, dst_len,
                                  src, src_len,
                                  parsed_dict, print_level + 2);

    if (contentChecksum_g) {
        print_log_to_both("%s\"Content Checksum\": {\n", print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"bit_size\": 32,\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"value\": [\n", print_level_tabel[print_level + 3]);
        print_hex_with_buffer((unsigned char *)src + src_offset, 4, print_level+4);
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
    char *arg, *name = NULL, *dic_name = NULL;
    size_t len = 0;
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
            if (dic_name != NULL) {
                fprintf(stderr, "only one or two files name allowed\n");
                return 3;
            }
            else {
                dic_name = arg;
            }
        }
        else
            name = arg;
    
    buffer_s const input = read_file(name);

    buffer_s dict = { NULL, 0 };
    if (dic_name) {
        dict = read_file(dic_name);
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
        strcat (compressed_log_file_name, "zstd_compressed.json");
        strcat (decompressed_log_file_name, "zstd_decompressed.json");
        strcat (decompressed_file_name, "zstd_decompressed.bin");
    }

    compressed_data_log_file = fopen(compressed_log_file_name, "w");

    size_t out_capacity = ZSTD_get_decompressed_size(input.address, input.size);
    if (out_capacity == (size_t)-1) {
        out_capacity = MAX_COMPRESSION_RATIO * input.size;
        fprintf(stderr, "WARNING: Compressed data does not contain "
                        "decompressed size, going to assume the compression "
                        "ratio is at most %d (decompressed size of at most "
                        "%u) \n",
                MAX_COMPRESSION_RATIO, (unsigned)out_capacity);
    }
    if (out_capacity > MAX_OUTPUT_SIZE)
        ERR_OUT("Required output size too large for this implementation \n");

    u8* const output = malloc(out_capacity);
    if (!output) ERR_OUT("failed to allocate memory \n");

    dictionary_t* const parsed_dict = create_dictionary();
    if (dict.size) {
        parse_dictionary(parsed_dict, dict.address, dict.size);
    }

    ret = zstd_dump(output, out_capacity, input.address, input.size, parsed_dict, 0);

    fclose(compressed_data_log_file);
    compressed_data_log_file = NULL;

    free_dictionary(parsed_dict);

    /* if requested, inflate again and write decompressed data to stdout */
    if (put) {
        // dest = malloc(destlen);
        // if (dest == NULL) {
        //     fprintf(stderr, "memory allocation failure\n");
        //     return 4;
        // }

        // decompressed_data_log_file = fopen(decompressed_log_file_name, "w");
        // zstd_dump(dest, &destlen, source, len, 0);
        // fclose(decompressed_data_log_file);

        // if (wr_file) {
        //     decompressed_data_file = fopen(decompressed_file_name, "wb");
        //     fwrite(dest, 1, destlen, decompressed_data_file);
        //     fclose(decompressed_data_file);
        // }

        // free(dest);
    }

    /* clean up */
    freeBuffer(input);
    freeBuffer(dict);
    free(output);

    return ret;
}
