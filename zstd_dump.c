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


local unsigned decode_zstd_header(const unsigned char *source, cJSON* json)
{
    unsigned char dic_id_flag, c_checksum_flag, unused_bit, single_segment_flag, frame_content_size_flag;
    unsigned char mantissa, exponent, window_log, window_base, window_add, window_size;
    unsigned int dic_id;
    unsigned long frame_content_size, fcs_value;
    unsigned char window_des_size, dic_id_size, frame_content_size_size;
    unsigned char flags;

    if (!source)
        return -1;

    cJSON* magic_number_json = cJSON_AddObjectToObject(json, "MAGIC NUMBER");
    cJSON_AddNumberToObject(magic_number_json, "bit_size", 32);
    dump_data_to_json(magic_number_json, "value", (unsigned char*)source, 4);

    flags = source[4];
    dic_id_flag = flags & 0x3;
    c_checksum_flag = (flags >> 2) & 0x1;
    unused_bit = (flags >> 4) & 0x1;
    single_segment_flag = (flags >> 5) & 0x1;
    frame_content_size_flag = (flags >> 6) & 0x3;
    contentChecksum_g = c_checksum_flag;
    cJSON* frame_header_json = cJSON_AddObjectToObject(json, "FRAME HEADER");
    cJSON* frame_header_descriptor_json = cJSON_AddObjectToObject(frame_header_json, "FRAME HEADER DESCRIPTOR");
    
    cJSON* dictionary_id_flag_json = cJSON_AddObjectToObject(frame_header_descriptor_json, "Dictionary ID Flag");
    cJSON_AddNumberToObject(dictionary_id_flag_json, "bit_size", 2);
    cJSON_AddNumberToObject(dictionary_id_flag_json, "value", dic_id_flag);
    if (dic_id_flag == 0) {
        dic_id_size = 0;
    }
    else if (dic_id_flag == 1) {
        dic_id_size = 1;
    }
    else if (dic_id_flag == 2) {
        dic_id_size = 2;
    }
    else if (dic_id_flag == 3) {
        dic_id_size = 4;
    }
    addStringToObjectFormatted(dictionary_id_flag_json, "description", "DID_Field_Size = %d", dic_id_size);

    cJSON* content_checksum_flag_json = cJSON_AddObjectToObject(frame_header_descriptor_json, "CONTENT CHECKSUM FLAG");
    cJSON_AddNumberToObject(content_checksum_flag_json, "bit_size", 1);
    cJSON_AddNumberToObject(content_checksum_flag_json, "value", c_checksum_flag);
    
    cJSON* reserved_json = cJSON_AddObjectToObject(frame_header_descriptor_json, "RESERVED");
    cJSON_AddNumberToObject(reserved_json, "bit_size", 1);
    
    cJSON* unused_bit_json = cJSON_AddObjectToObject(frame_header_descriptor_json, "UNUSED BIT");
    cJSON_AddNumberToObject(unused_bit_json, "bit_size", 1);
    cJSON_AddNumberToObject(unused_bit_json, "value", unused_bit);

    cJSON* single_segment_flag_json = cJSON_AddObjectToObject(frame_header_descriptor_json, "Single Segment Flag");
    cJSON_AddNumberToObject(single_segment_flag_json, "bit_size", 1);
    cJSON_AddNumberToObject(single_segment_flag_json, "value", single_segment_flag);
    if (single_segment_flag == 0) {
        cJSON_AddStringToObject(single_segment_flag_json, "description", "data don't need be regenerated within a single continuous memory segment");
    }
    else if (single_segment_flag == 1) {
        cJSON_AddStringToObject(single_segment_flag_json, "description", "data must be regenerated within a single continuous memory segment");
    }
    
    cJSON* frame_content_size_flag_json = cJSON_AddObjectToObject(frame_header_descriptor_json, "Frame Content Size Flag");
    cJSON_AddNumberToObject(frame_content_size_flag_json, "bit_size", 2);
    cJSON_AddNumberToObject(frame_content_size_flag_json, "value", frame_content_size_flag);
    if (frame_content_size_flag == 0) {
        if (single_segment_flag == 1) {
            frame_content_size_size = 1;
        }
        else {
            frame_content_size_size = 0;
        }
    }
    else if (frame_content_size_flag == 1) {
        frame_content_size_size = 2;
    }
    else if (frame_content_size_flag == 2) {
        frame_content_size_size = 4;
    }
    else if (frame_content_size_flag == 3) {
        frame_content_size_size = 8;
    }
    addStringToObjectFormatted(frame_content_size_flag_json, "description", "FCS_Field_Size = %d", frame_content_size_size);

    if (single_segment_flag == 0) {
        window_des_size = 1;
        
        flags = source[5];
        mantissa = flags & 0x7;
        exponent = (flags >> 3);
        window_log = 10 + exponent;
        window_base = 1 << window_log;
        window_add = (window_base / 8) * mantissa;
        window_size = window_base + window_add;

        cJSON* window_descriptor_json = cJSON_AddObjectToObject(frame_header_json, "Window Descriptor");
        cJSON* mantissa_json = cJSON_AddObjectToObject(window_descriptor_json, "Mantissa");
        cJSON_AddNumberToObject(mantissa_json, "bit_size", 3);
        cJSON_AddNumberToObject(mantissa_json, "value", mantissa);

        cJSON* exponent_json = cJSON_AddObjectToObject(window_descriptor_json, "Exponent");
        cJSON_AddNumberToObject(exponent_json, "bit_size", 5);
        cJSON_AddNumberToObject(exponent_json, "value", exponent);
        
        addStringToObjectFormatted(window_descriptor_json, "description", "window size = %d", window_size);
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

        cJSON* dictionary_id_json = cJSON_AddObjectToObject(frame_header_json, "Dictionary ID");
        cJSON_AddNumberToObject(dictionary_id_json, "bit_size", dic_id_size * 8);
        cJSON_AddNumberToObject(dictionary_id_json, "value", dic_id);
    }

    if (frame_content_size_size > 0) {
        cJSON* frame_content_size_json = cJSON_AddObjectToObject(frame_header_json, "Frame Content Size");
        cJSON_AddNumberToObject(frame_content_size_json, "bit_size", frame_content_size_size * 8);
        if (frame_content_size_size == 1) {
            fcs_value = source[5 + window_des_size + dic_id_size];
            frame_content_size = fcs_value;
            cJSON_AddNumberToObject(frame_content_size_json, "value", fcs_value);
            addStringToObjectFormatted(frame_content_size_json, "description", "The original (uncompressed) size is %d", frame_content_size);
        }
        else if (frame_content_size_size == 2)
        {
            fcs_value = *(unsigned short *)(source + 5 + window_des_size + dic_id_size);
            frame_content_size = fcs_value + 256;
            cJSON_AddNumberToObject(frame_content_size_json, "value", fcs_value);
            addStringToObjectFormatted(frame_content_size_json, "description", "The original (uncompressed) size is (256+%d)=%d", fcs_value, frame_content_size);
        }
        else if (frame_content_size_size == 4)
        {
            fcs_value = *(unsigned int *)(source + 5 + window_des_size + dic_id_size);
            frame_content_size = fcs_value;
            cJSON_AddNumberToObject(frame_content_size_json, "value", fcs_value);
            addStringToObjectFormatted(frame_content_size_json, "description", "The original (uncompressed) size is %d", frame_content_size);
        }
        else if (frame_content_size_size == 8)
        {
            fcs_value = *(unsigned long *)(source + 5 + window_des_size + dic_id_size);
            frame_content_size = fcs_value;
            cJSON_AddNumberToObject(frame_content_size_json, "value", fcs_value);
            addStringToObjectFormatted(frame_content_size_json, "description", "The original (uncompressed) size is %d", frame_content_size);
        }
    }

    return window_des_size + dic_id_size + frame_content_size_size + 5;
}

int zstd_dump(void *const dst, const size_t dst_len,
              const void *const src, const size_t src_len,
              dictionary_t* parsed_dict,
              cJSON* json)
{
    int ret = 0;
    unsigned zstd_header_size;
    unsigned zstd_blocks_size;

    cJSON* zstd_format_json = cJSON_AddObjectToObject(json, "ZSTD_FORMAT");
    cJSON* zstd_header_json = cJSON_AddObjectToObject(zstd_format_json, "ZSTD_HEADER");

    zstd_header_size = decode_zstd_header(src, zstd_header_json);
    if (zstd_header_size == 0) {
        return -1;
    }

    size_t const src_offset =
        ZSTD_decompress_with_dict(dst, dst_len,
                                  src, src_len,
                                  parsed_dict, zstd_format_json);

    if (contentChecksum_g) {
        cJSON* content_checksum_json = cJSON_AddObjectToObject(zstd_format_json, "Content Checksum");
        cJSON_AddNumberToObject(content_checksum_json, "bit_size", 32);
        dump_data_to_json(content_checksum_json, "value", (unsigned char*)src + src_offset, 4);
    }

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
    compressed_data_json = cJSON_CreateObject();

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

    ret = zstd_dump(output, out_capacity, input.address, input.size, parsed_dict, compressed_data_json);

    char* jsonString = cJSON_Print(compressed_data_json);
    if (compressed_data_log_file) {
        fprintf(compressed_data_log_file, "%s", jsonString);
    }
    cJSON_free(jsonString);
    cJSON_Delete(compressed_data_json);
    fclose(compressed_data_log_file);
    compressed_data_log_file = NULL;
    compressed_data_json = NULL;

    free_dictionary(parsed_dict);

    /* if requested, inflate again and write decompressed data to stdout */
    if (put) {

        if (wr_file) {
            decompressed_data_file = fopen(decompressed_file_name, "wb");
            fwrite(output, 1, out_capacity, decompressed_data_file);
            fclose(decompressed_data_file);
        }
    }

    /* clean up */
    freeBuffer(input);
    freeBuffer(dict);
    free(output);

    return ret;
}
