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

local unsigned decode_lz4_header(const unsigned char *source, cJSON* json)
{
    unsigned char dictId, c_checksum, c_size, b_checksum, b_indep, version, reserved;
    unsigned char block_max_size;
    unsigned char hc;
    unsigned char c_size_size, d_id_size;
    unsigned char flags;

    if (!source)
        return -1;

    cJSON* magic_number_json = cJSON_AddObjectToObject(json, "MAGIC_NUMBER");
    cJSON_AddNumberToObject(magic_number_json, "bit_size", 32);
    dump_data_to_json(magic_number_json, "value", (unsigned char *)source, 4);

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

    cJSON* frame_descriptor_json = cJSON_AddObjectToObject(json, "FRAME DESCRIPTOR");
    
    cJSON* flg_json = cJSON_AddObjectToObject(frame_descriptor_json, "FLG");

    cJSON* dictionary_id_flag_json = cJSON_AddObjectToObject(frame_descriptor_json, "Dictionary ID Flag");
    cJSON_AddNumberToObject(dictionary_id_flag_json, "bit_size", 1);
    cJSON_AddNumberToObject(dictionary_id_flag_json, "value", dictId);
    if (dictId == 0) {
        cJSON_AddStringToObject(dictionary_id_flag_json, "description", "a 4-bytes Dict-ID field will not be present");
    }
    else if (dictId == 1) {
        cJSON_AddStringToObject(dictionary_id_flag_json, "description", "a 4-bytes Dict-ID field will be present, after the descriptor flags and the Content Size");
    }

    cJSON* reserved_json = cJSON_AddObjectToObject(flg_json, "RESERVED");
    cJSON_AddNumberToObject(reserved_json, "bit_size", 1);

    cJSON* content_checksum_flag_json = cJSON_AddObjectToObject(flg_json, "Content Checksum Flag");
    cJSON_AddNumberToObject(content_checksum_flag_json, "bit_size", 1);
    cJSON_AddNumberToObject(content_checksum_flag_json, "value", c_checksum);
    if (c_checksum == 0) {
        cJSON_AddStringToObject(content_checksum_flag_json, "description", "a 32-bits content checksum will not be appended after the EndMark");
    }
    else if (c_checksum == 1) {
        cJSON_AddStringToObject(content_checksum_flag_json, "description", "a 32-bits content checksum will be appended after the EndMark");
    }

    cJSON* content_size_flag_json = cJSON_AddObjectToObject(flg_json, "Content Size Flag");
    cJSON_AddNumberToObject(content_size_flag_json, "bit_size", 1);
    cJSON_AddNumberToObject(content_size_flag_json, "value", c_size);
    if (c_size == 0) {
        cJSON_AddStringToObject(content_size_flag_json, "description", "the uncompressed size of data included within the frame will not be present as an 8 bytes unsigned little endian value");
    }
    else if (c_size == 1) {
        cJSON_AddStringToObject(content_size_flag_json, "description", "the uncompressed size of data included within the frame will be present as an 8 bytes unsigned little endian value, after the flags");
    }

    cJSON* block_checksum_flag_json = cJSON_AddObjectToObject(flg_json, "Block checksum flag");
    cJSON_AddNumberToObject(block_checksum_flag_json, "bit_size", 1);
    cJSON_AddNumberToObject(block_checksum_flag_json, "value", b_checksum);
    if (b_checksum == 0) {
        cJSON_AddStringToObject(block_checksum_flag_json, "description", "each data block will not be followed by a 4-bytes checksum");
    }
    else if (b_checksum == 1) {
        cJSON_AddStringToObject(block_checksum_flag_json, "description", "each data block will be followed by a 4-bytes checksum");
    }

    cJSON* block_indep_flag_json = cJSON_AddObjectToObject(flg_json, "Block Independence Flag");
    cJSON_AddNumberToObject(block_indep_flag_json, "bit_size", 1);
    cJSON_AddNumberToObject(block_indep_flag_json, "value", b_indep);
    if (b_indep == 1) {
        cJSON_AddStringToObject(block_indep_flag_json, "description", "blocks are independent.");
    }
    else if (b_indep == 0) {
        cJSON_AddStringToObject(block_indep_flag_json, "description", "each block depends on previous ones(up to LZ4 window size, which is 64 KB).");
    }

    cJSON* version_number_json = cJSON_AddObjectToObject(flg_json, "Version Number");
    cJSON_AddNumberToObject(version_number_json, "bit_size", 2);
    cJSON_AddNumberToObject(version_number_json, "value", version);
    cJSON_AddStringToObject(version_number_json, "description", "2 bits filed, must be set to 01.");

    flags = source[5];
    block_max_size = (flags >> 4) & 0x7;
    cJSON* db_json = cJSON_AddObjectToObject(frame_descriptor_json, "DB");

    cJSON* rsvd0_json = cJSON_AddObjectToObject(db_json, "RSVD0");
    cJSON_AddNumberToObject(rsvd0_json, "bit_size", 4);
    
    cJSON* block_max_size_json = cJSON_AddObjectToObject(db_json, "BLOCK MAXSIZE");
    cJSON_AddNumberToObject(block_max_size_json, "bit_size", 3);
    cJSON_AddNumberToObject(block_max_size_json, "value", block_max_size);
    if (block_max_size == 4) {
        cJSON_AddStringToObject(block_max_size_json, "description", "64KB");
    }
    else if (block_max_size == 5) {
        cJSON_AddStringToObject(block_max_size_json, "description", "256KB");
    }
    else if (block_max_size == 6) {
        cJSON_AddStringToObject(block_max_size_json, "description", "1MB");
    }
    else if (block_max_size == 7) {
        cJSON_AddStringToObject(block_max_size_json, "description", "4MB");
    }
    
    cJSON* rsvd1_json = cJSON_AddObjectToObject(db_json, "RSVD1");
    cJSON_AddNumberToObject(rsvd1_json, "bit_size", 1);
    
    if (c_size == 1) {
        c_size_size = 8;

        cJSON* content_size_json = cJSON_AddObjectToObject(frame_descriptor_json, "Content Size");
        cJSON_AddNumberToObject(content_size_json, "bit_size", 64);
        dump_data_to_json(content_size_json, "value", (unsigned char*)source + 6, 8);
    }
    else {
        c_size_size = 0;
    }
    
    if (dictId == 1) {
        d_id_size = 4;

        cJSON* dictionary_id_json = cJSON_AddObjectToObject(frame_descriptor_json, "Dictionary ID");
        cJSON_AddNumberToObject(dictionary_id_json, "bit_size", 32);
        dump_data_to_json(dictionary_id_json, "value", (unsigned char*)source + c_size_size + 6, 4);
    }
    else {
        d_id_size = 0;
    }

    hc = source[6 + c_size_size + d_id_size];
    cJSON* header_checksum_json = cJSON_AddObjectToObject(frame_descriptor_json, "Header Checksum");
    cJSON_AddNumberToObject(header_checksum_json, "bit_size", 8);
    cJSON_AddNumberToObject(header_checksum_json, "value", hc);

    return c_size_size + d_id_size + 7;
}

int decode_lz4_block(const unsigned char *source, cJSON* json)
{
    unsigned int eof, block_size, byte_count;
    unsigned char compressed_flag;
    
    eof = *(unsigned int*)source;

    if (eof == 0) {
        return 0;
    }

    byte_count = 0;

    cJSON* blocks_json = cJSON_AddArrayToObject(json, "LZ4_BLOCK");

    do
    {
        cJSON* block_json = cJSON_CreateObject();

        cJSON_AddNumberToObject(block_json, "BLOCK_BIT_POSITION", byte_count * 8);

        block_size = *(unsigned int*)source;
        compressed_flag = 0x1 & (block_size >> 31);
        block_size = (block_size << 1) >> 1;
        cJSON_AddNumberToObject(block_json, "BLOCK_BIT_SIZE", (block_size + 4) * 8);
        if (compressed_flag == 1) {
            cJSON_AddStringToObject(block_json, "COMPRESSED_FLAG", "UNCOMPRESSED");
        }
        else if (compressed_flag == 0){
            cJSON_AddStringToObject(block_json, "COMPRESSED_FLAG", "COMPRESSED");
        }
        // TODO decode
        
        if (blockChecksum_g == 1) {
            dump_data_to_json(block_json, "block_checksum_json", (unsigned char*)source + 4 + block_size, 4);

            byte_count += 4;
            source += 4;
        }
        
        byte_count += block_size + 4;
        source += block_size + 4;

        cJSON_AddItemToArray(blocks_json, block_json);
    } while (*(unsigned int*) source != 0);

    return byte_count;
}

int lz4_dump(unsigned char *dest,
              unsigned long *destlen,
              const unsigned char *source,
              unsigned long sourcelen,
              cJSON* json)
{
    int ret = 0;
    unsigned lz4_header_size;
    unsigned lz4_blocks_size;

    cJSON* lz4_format_json = cJSON_AddObjectToObject(json, "ZLIB_FORMAT");
    cJSON* lz4_header_json = cJSON_AddObjectToObject(lz4_format_json, "LZ4_HEADER");

    lz4_header_size = decode_lz4_header(source, lz4_header_json);
    if (lz4_header_size == 0) {
        return -1;
    }

    lz4_blocks_size = decode_lz4_block(source + lz4_header_size, lz4_format_json);

    cJSON* eof_json = cJSON_AddObjectToObject(lz4_format_json, "EOF");
    cJSON_AddNumberToObject(eof_json, "bit_size", 32);
    dump_data_to_json(eof_json, "value", (unsigned char *)source + lz4_header_size + lz4_blocks_size, 4);

    if (contentChecksum_g) {
        cJSON* content_checksum_json = cJSON_AddObjectToObject(lz4_format_json, "CONTENT CHECKSUM");
        cJSON_AddNumberToObject(content_checksum_json, "bit_size", 32);
        dump_data_to_json(content_checksum_json, "value", (unsigned char *)source + lz4_header_size + lz4_blocks_size + 4, 4);
    }

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
    compressed_data_json = cJSON_CreateObject();

    ret = lz4_dump(NIL, &destlen, source, len, compressed_data_json);

    char* jsonString = cJSON_Print(compressed_data_json);
    if (compressed_data_log_file) {
        fprintf(compressed_data_log_file, "%s", jsonString);
    }
    cJSON_free(jsonString);
    cJSON_Delete(compressed_data_json);
    fclose(compressed_data_log_file);
    compressed_data_log_file = NULL;
    compressed_data_json = NULL;

    /* if requested, inflate again and write decompressed data to stdout */
    if (put && ret == 0) {
        dest = malloc(destlen);
        if (dest == NULL) {
            fprintf(stderr, "memory allocation failure\n");
            return 4;
        }

        decompressed_data_log_file = fopen(decompressed_log_file_name, "w");
        decompressed_data_json = cJSON_CreateObject();
        lz4_dump(dest, &destlen, source, len, decompressed_data_json);
        char* jsonString = cJSON_Print(decompressed_data_json);
        if (decompressed_data_log_file) {
            fprintf(decompressed_data_log_file, "%s", jsonString);
        }
        cJSON_free(jsonString);
        cJSON_Delete(decompressed_data_json);
        fclose(decompressed_data_log_file);
        decompressed_data_log_file = NULL;
        decompressed_data_json = NULL;

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
