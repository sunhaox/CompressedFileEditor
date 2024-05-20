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
   of input to skip before inflating (e.g. to skip a gzip or gzip header), and
   -w is used to write the decompressed data to stdout.  -f is for coverage
   testing, and causes pufftest to fail with not enough output space (-f does
   a write like -w, so -w is not required). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

local int decode_gzip_header(const unsigned char *source, cJSON* json)
{
    unsigned char comp_method, file_flags, compression_flags, os_type;
    unsigned int modification_time;
    time_t t;
    struct tm  ts;
    char buf[80];
    unsigned char string_len;
    unsigned int buffer_index = 0, data_num = 0;

    if (!source)
        return -1;

    if (source[buffer_index] != 0x1f) {
        fprintf(stderr, "gzip header decode ID1 failed!\n");
        return -1;
    } else {
        cJSON* id1_json = cJSON_AddObjectToObject(json, "ID1");
        cJSON_AddNumberToObject(id1_json, "bit_size", 8);
        cJSON_AddNumberToObject(id1_json, "value", source[buffer_index]);
        cJSON_AddStringToObject(id1_json, "description", "fixed value");
    }

    buffer_index++;
    if (source[buffer_index] != 0x8b) {
        fprintf(stderr, "gzip header decode ID2 failed!\n");
        return -1;
    } else {
        cJSON* id2_json= cJSON_AddObjectToObject(json, "ID2");
        cJSON_AddNumberToObject(id2_json, "bit_size", 8);
        cJSON_AddNumberToObject(id2_json, "value", source[buffer_index]);
        cJSON_AddStringToObject(id2_json, "description", "fixed value");
    }

    buffer_index++;
    comp_method = source[buffer_index];

    cJSON* compression_method_json = cJSON_AddObjectToObject(json, "COMPRESSION_METHOD");
    cJSON_AddNumberToObject(compression_method_json, "bit_size", 8);
    cJSON_AddNumberToObject(compression_method_json, "value", comp_method);
    if (comp_method == 8) {
        cJSON_AddStringToObject(compression_method_json, "description", "DEFLATE");
    } else if (comp_method < 8) {
        cJSON_AddStringToObject(compression_method_json, "description", "Reserved");
    } else {
        cJSON_AddStringToObject(compression_method_json, "description", "Invalid");
        fprintf(stderr, "gzip header decode failed!\n");
        return -1;
    }

    buffer_index++;
    file_flags = source[buffer_index];

    cJSON* file_flags_json = cJSON_AddObjectToObject(json, "file_flags");

    cJSON* ftext_json = cJSON_AddObjectToObject(file_flags_json, "FTEXT");
    cJSON_AddNumberToObject(ftext_json, "bit_size", 1);
    cJSON_AddNumberToObject(ftext_json, "value", file_flags & 0x1);
    if (file_flags & 0x1) {
        cJSON_AddStringToObject(ftext_json, "description", "ASCII text");
    } else {
        cJSON_AddStringToObject(ftext_json, "description", "binary data");
    }

    cJSON* fhcrc_json = cJSON_AddObjectToObject(file_flags_json, "FHCRC");
    cJSON_AddNumberToObject(fhcrc_json, "bit_size", 1);
    cJSON_AddNumberToObject(fhcrc_json, "value", (file_flags >> 1) & 0x1);
    if ((file_flags >> 1) & 0x1) {
        cJSON_AddStringToObject(fhcrc_json, "description", "CRC16 for the gzip header is present");
    } else {
        cJSON_AddStringToObject(fhcrc_json, "description", "CRC16 for the gzip header is not present");
    }

    cJSON* fextra_json = cJSON_AddObjectToObject(file_flags_json, "FEXTRA");
    cJSON_AddNumberToObject(fextra_json, "bit_size", 1);
    cJSON_AddNumberToObject(fextra_json, "value", (file_flags >> 2) & 0x1);
    if ((file_flags >> 2) & 0x1) {
        cJSON_AddStringToObject(fextra_json, "description", "optional extra fields are present");
    } else {
        cJSON_AddStringToObject(fextra_json, "description", "optional extra fields are not present");
    }

    cJSON* fname_json = cJSON_AddObjectToObject(file_flags_json, "FNAME");
    cJSON_AddNumberToObject(fname_json, "bit_size", 1);
    cJSON_AddNumberToObject(fname_json, "value", (file_flags >> 3) & 0x1);
    if ((file_flags >> 3) & 0x1) {
        cJSON_AddStringToObject(fname_json, "description", "original file name is present");
    } else {
        cJSON_AddStringToObject(fname_json, "description", "original file name is not present");
    }

    cJSON* fcomment_json = cJSON_AddObjectToObject(file_flags_json, "FCOMMENT");
    cJSON_AddNumberToObject(fcomment_json, "bit_size", 1);
    cJSON_AddNumberToObject(fcomment_json, "value", (file_flags >> 4) & 0x1);
    if ((file_flags >> 4) & 0x1) {
        cJSON_AddStringToObject(fcomment_json, "description", "zero-terminated file comment is present");
    } else {
        cJSON_AddStringToObject(fcomment_json, "description", "zero-terminated file comment is not present");
    }

    cJSON* reserved_json = cJSON_AddObjectToObject(file_flags_json, "RESERVED");
    cJSON_AddNumberToObject(reserved_json, "bit_size", 3);
    cJSON_AddNumberToObject(reserved_json, "value", (file_flags >> 5) & 0x7);

    if ((file_flags >> 5) & 0x7) {
        cJSON_AddStringToObject(reserved_json, "description", "reserved bits should be 0!");
        fprintf(stderr, "gzip header decode failed!\n");
        return -1;
    } else {
        cJSON_AddStringToObject(reserved_json, "description", "reserved");
    }

    buffer_index++;
    modification_time = source[4] + (source[5] << 8) + (source[6] << 16) + (source[7] << 24);
    buffer_index += 4;

    cJSON* mtime_json = cJSON_AddObjectToObject(json, "MTIME");
    cJSON_AddNumberToObject(mtime_json, "bit_size", 32);
    cJSON_AddNumberToObject(mtime_json, "value", modification_time);
    if (modification_time) {
        t = modification_time;
        ts = *localtime(&t);
        strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S", &ts);
        cJSON_AddStringToObject(mtime_json, "description", buf);
    } else {
        cJSON_AddStringToObject(mtime_json, "description", "no time stamp is available");
    }

    compression_flags = source[buffer_index];
    cJSON* xfl_json = cJSON_AddObjectToObject(json, "XFL");
    cJSON_AddNumberToObject(xfl_json, "bit_size", 8);
    cJSON_AddNumberToObject(xfl_json, "value", compression_flags);
    if (compression_flags == 2) {
        cJSON_AddStringToObject(xfl_json, "description", "maximum compression, slowest algorithm");
    } else if (compression_flags == 4) {
        cJSON_AddStringToObject(xfl_json, "description", "fastest algorithm");
    } else {
        cJSON_AddStringToObject(xfl_json, "description", "compression flags");
    }

    buffer_index++;
    os_type = source[buffer_index];
    cJSON* os_json = cJSON_AddObjectToObject(json, "OS");
    cJSON_AddNumberToObject(os_json, "bit_size", 8);
    cJSON_AddNumberToObject(os_json, "value", os_type);
    switch (os_type) {
        case 0:
            cJSON_AddStringToObject(os_json, "description", "FAT filesystem (MS-DOS, OS/2, NT/Win32");
            break;
        case 1:
            cJSON_AddStringToObject(os_json, "description", "Amiga");
            break;
        case 2:
            cJSON_AddStringToObject(os_json, "description", "VMS (or OpenVMS)");
            break;
        case 3:
            cJSON_AddStringToObject(os_json, "description", "Unix");
            break;
        case 4:
            cJSON_AddStringToObject(os_json, "description", "VM/CMS");
            break;
        case 5:
            cJSON_AddStringToObject(os_json, "description", "Atari TOS");
            break;
        case 6:
            cJSON_AddStringToObject(os_json, "description", "HPFS filesystem (OS/2, NT)");
            break;
        case 7:
            cJSON_AddStringToObject(os_json, "description", "Macintosh");
            break;
        case 8:
            cJSON_AddStringToObject(os_json, "description", "Z-System");
            break;
        case 9:
            cJSON_AddStringToObject(os_json, "description", "CP/M");
            break;
        case 10:
            cJSON_AddStringToObject(os_json, "description", "TOPS-20");
            break;
        case 11:
            cJSON_AddStringToObject(os_json, "description", "NTFS filesystem (NT)");
            break;
        case 12:
            cJSON_AddStringToObject(os_json, "description", "QDOS");
            break;
        case 13:
            cJSON_AddStringToObject(os_json, "description", "Acorn RISCOS");
            break;

        default:
            cJSON_AddStringToObject(os_json, "description", "unknown OS");
            break;
    }

    buffer_index++;
    if ((file_flags >> 2) & 0x1) {

        data_num = source[buffer_index];
        buffer_index++;
        data_num |= (source[buffer_index] << 8);
        buffer_index++;

        cJSON* xlen_json = cJSON_AddObjectToObject(json, "XLEN");
        cJSON_AddNumberToObject(xlen_json, "bit_size", 16);
        cJSON_AddNumberToObject(xlen_json, "value", data_num);
        cJSON_AddStringToObject(xlen_json, "description", "bytes of extra field");
        
        cJSON* extra_json = cJSON_AddObjectToObject(json, "EXTRA");
        cJSON_AddNumberToObject(extra_json, "bit_size", data_num<<3);
        dump_data_to_json(extra_json, "value", (unsigned char *)&source[buffer_index], data_num);

        buffer_index += data_num;
    }

    if ((file_flags >> 3) & 0x1) {
        cJSON* fname_json = cJSON_AddObjectToObject(json, "FNAME");
        cJSON_AddNumberToObject(fname_json, "bit_size", string_len << 3);
        cJSON_AddStringToObject(fname_json, "value", &source[buffer_index]);
        buffer_index += string_len;
    }

    if ((file_flags >> 4) & 0x1) {
        cJSON* fcomment_json = cJSON_AddObjectToObject(json, "FCOMMENT");
        cJSON_AddNumberToObject(fcomment_json, "bit_size", string_len << 3);
        cJSON_AddStringToObject(fcomment_json, "value", &source[buffer_index]);
        buffer_index += string_len;
    }

    if ((file_flags >> 1) & 0x1) {
        cJSON* fhcrc_json = cJSON_AddObjectToObject(json, "FHCRC");
        cJSON_AddNumberToObject(fhcrc_json, "bit_size", 16);
        dump_data_to_json(fhcrc_json, "value", (unsigned char *)&source[buffer_index], 2);
        buffer_index += 2;
    }

    return buffer_index;
}

int gzip_dump(unsigned char *dest,
              unsigned long *destlen,
              const unsigned char *source,
              unsigned long sourcelen,
              cJSON* json)
{
    int ret;
    unsigned gzip_header_size;
    unsigned gzip_footer_size = 4;
    unsigned source_skip_header_size;

    unsigned long decompressed_len;
    int print_level = 0;

    cJSON *gzip_format_json = cJSON_AddObjectToObject(json, "GZIP_FORMAT");
    cJSON *gzip_header_json = cJSON_AddObjectToObject(gzip_format_json, "GZIP_HEADER");

    ret = decode_gzip_header(source, gzip_header_json);
    if (ret < 0) {
        return -1;
    } else {
        gzip_header_size = ret;
    }

    source_skip_header_size = sourcelen - gzip_header_size;
    decompressed_len = source_skip_header_size;

    ret = puff(dest, destlen, source + gzip_header_size, &decompressed_len,
        gzip_format_json);
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

    if (source_skip_header_size - decompressed_len >= 4) {
        print_log_to_both("%s\"CHECKSUM_IN_FILE\": {\n", print_level_tabel[print_level + 2]);
        print_log_to_both("%s\"bit_size\": 32,\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"value\": [\n", print_level_tabel[print_level + 3]);
        print_hex_with_buffer((unsigned char *)source + gzip_header_size + decompressed_len, 4, print_level + 4);
        print_log_to_both("%s],\n", print_level_tabel[print_level + 3]);
        print_log_to_both("%s\"description\": \"CRC-32 Checksum in File\"\n", print_level_tabel[print_level + 3]);
        if (dest) {
            print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
            print_log_to_both("%s\"CHECKSUM_CALCULATED\": {\n", print_level_tabel[print_level + 2]);
            print_log_to_both("%s\"value\": [\n", print_level_tabel[print_level + 3]);
            adler32_checksum = swap_uint32(adler32_checksum);
            print_hex_with_buffer((unsigned char *)&adler32_checksum, 4, print_level + 4);
            print_log_to_both("%s],\n", print_level_tabel[print_level + 3]);
            print_log_to_both("%s\"description\": \"CRC-32 Checksum Calculated\"\n", print_level_tabel[print_level + 3]);
        }

        if (source_skip_header_size - decompressed_len == 8) {
            print_log_to_both("%s},\n", print_level_tabel[print_level + 2]);
            print_log_to_both("%s\"INPUT_SIZE\": {\n", print_level_tabel[print_level + 2]);
            print_log_to_both("%s\"bit_size\": %d,\n",
                print_level_tabel[print_level + 3], 32);
            print_log_to_both("%s\"value\": %d\n",
                print_level_tabel[print_level + 3], *((int *)&source[gzip_header_size + decompressed_len + 4]));
        }

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
        strcat (compressed_log_file_name, "gzip_compressed.json");
        strcat (decompressed_log_file_name, "gzip_decompressed.json");
        strcat (decompressed_file_name, "gzip_decompressed.bin");
    }

    compressed_data_log_file = fopen(compressed_log_file_name, "w");
    compressed_data_json = cJSON_CreateObject();

    ret = gzip_dump(NIL, &destlen, source, len, compressed_data_json);

    char* jsonString = cJSON_Print(compressed_data_json);
    printf("%s", jsonString);
    cJSON_free(jsonString);
    cJSON_Delete(compressed_data_json);
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
        gzip_dump(dest, &destlen, source, len, 0);
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
