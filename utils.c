#include "utils.h"

FILE *compressed_data_log_file = NULL;
FILE *decompressed_data_log_file = NULL;
FILE *decompressed_data_file = NULL;

cJSON *compressed_data_json = NULL;
cJSON *decompressed_data_json = NULL;

unsigned char print_data_verbose = 0;

unsigned int compressed_data_print_num_count = 0;
unsigned int decompressed_data_print_num_count = 0;

unsigned int compressed_data_print_data_count = 0;
unsigned int decompressed_data_print_data_count = 0;

unsigned char compressed_data_print_buffer[200] = {0};
unsigned char decompressed_data_print_buffer[200] = {0};

unsigned int adler32_checksum = 1;

char *print_level_tabel[] = {"",
                       "\t",
                       "\t\t",
                       "\t\t\t",
                       "\t\t\t\t",
                       "\t\t\t\t\t",
                       "\t\t\t\t\t\t",
                       "\t\t\t\t\t\t\t",
                       "\t\t\t\t\t\t\t\t",
                       "\t\t\t\t\t\t\t\t\t",
                       "\t\t\t\t\t\t\t\t\t\t",
                       "\t\t\t\t\t\t\t\t\t\t\t",
                       };

unsigned int swap_uint32(unsigned int val)
{
    val = ((val << 8) & 0xFF00FF00 ) | ((val >> 8) & 0xFF00FF);
    return (val << 16) | (val >> 16);
}

void print_to_compressed_log(char *fmt, ...)
{
    va_list args;
    if (compressed_data_log_file) {
        va_start(args, fmt);
        vfprintf(compressed_data_log_file, fmt, args);
        va_end(args);
    }
}

void print_to_decompressed_log(char *fmt, ...)
{
    va_list args;
    if (decompressed_data_log_file) {
        va_start(args, fmt);
        vfprintf(decompressed_data_log_file, fmt, args);
        va_end(args);
    }
}

void print_compressed_data_hex(int data_val, int print_level)
{
    if (print_data_verbose) {
        compressed_data_print_num_count += sprintf(compressed_data_print_buffer
            + compressed_data_print_num_count, "0x%02x ", data_val);
        compressed_data_print_data_count++;

        if (compressed_data_print_data_count == 16) {
            print_to_compressed_log("%s\"%s\",\n", print_level_tabel[print_level],
                    compressed_data_print_buffer);
            memset(compressed_data_print_buffer, 0,
                compressed_data_print_num_count);
            compressed_data_print_data_count = 0;
            compressed_data_print_num_count = 0;
        }
    }
}

void print_compressed_data_dec(int data_val, int print_level)
{
    if (print_data_verbose) {
        compressed_data_print_num_count += sprintf(compressed_data_print_buffer
            + compressed_data_print_num_count, "%d ", data_val);
        compressed_data_print_data_count++;

        if (compressed_data_print_data_count == 16) {
            print_to_compressed_log("%s\"%s\",\n", print_level_tabel[print_level],
                    compressed_data_print_buffer);
            memset(compressed_data_print_buffer, 0,
                compressed_data_print_num_count);
            compressed_data_print_data_count = 0;
            compressed_data_print_num_count = 0;
        }
    }
}

void print_decompressed_data_hex(int data_val, int print_level)
{
    if (print_data_verbose) {
        decompressed_data_print_num_count += sprintf(decompressed_data_print_buffer
            + decompressed_data_print_num_count, "0x%02x ", data_val);
        decompressed_data_print_data_count++;

        if (decompressed_data_print_data_count == 16) {
            print_to_decompressed_log("%s\"%s\",\n", print_level_tabel[print_level],
                decompressed_data_print_buffer);
            memset(decompressed_data_print_buffer, 0,
                decompressed_data_print_num_count);
            decompressed_data_print_data_count = 0;
            decompressed_data_print_num_count = 0;
        }
    }
}

void print_compressed_data_final(int print_level)
{
    if (print_data_verbose) {
        if (compressed_data_print_num_count) {
            print_to_compressed_log("%s\"%s\"\n", print_level_tabel[print_level],
                    compressed_data_print_buffer);
            memset(compressed_data_print_buffer, 0,
                compressed_data_print_num_count);
            compressed_data_print_data_count = 0;
            compressed_data_print_num_count = 0;
        } else {
            print_to_compressed_log("%s\"\"\n", print_level_tabel[print_level]);
        }
    }
}

void print_decompressed_data_final(int print_level)
{
    if (print_data_verbose) {
        if (decompressed_data_print_num_count) {
            print_to_decompressed_log("%s\"%s\"\n", print_level_tabel[print_level],
                decompressed_data_print_buffer);
            memset(decompressed_data_print_buffer, 0,
                decompressed_data_print_num_count);
            decompressed_data_print_data_count = 0;
            decompressed_data_print_num_count = 0;
        } else {
            print_to_decompressed_log("%s\"\"\n", print_level_tabel[print_level]);
        }
    }
}

void print_log_to_both(char *fmt, ...)
{
    va_list args;
    if (compressed_data_log_file) {
        va_start(args, fmt);
        vfprintf(compressed_data_log_file, fmt, args);
        va_end(args);
    }

    if (decompressed_data_log_file) {
        va_start(args, fmt);
        vfprintf(decompressed_data_log_file, fmt, args);
        va_end(args);
    }
}

void dump_data_to_number_array_json(cJSON* json, const char *const name, unsigned char *buffer, unsigned int num)
{
    unsigned char print_buffer[200] = {0};
    unsigned int print_count = 0;
    unsigned int lines, remain, i, j;
    unsigned char data_val;

    lines = num >> 4;
    remain = num & 0xF;

    cJSON *array = cJSON_AddArrayToObject(json, name);

    for (i = 0; i < num; i++) {
        data_val = *(buffer + i);
        cJSON* item = cJSON_CreateNumber(data_val);
        cJSON_AddItemToArray(array, item);
    }

}

void dump_data_to_string_json(cJSON* json, const char *const name, unsigned char *buffer, unsigned int num)
{
    unsigned char print_buffer[200] = {0};
    unsigned int print_count = 0;
    unsigned int lines, remain, i, j;
    unsigned char data_val;

    lines = num >> 4;
    remain = num & 0xF;

    cJSON *array = cJSON_AddArrayToObject(json, name);

    for (i = 0; i < lines; i++) {
        for (j = 0; j < 16; j++) {
            data_val = *(buffer + (i << 4) + j);
            print_count += sprintf(print_buffer + print_count, "0x%02x ", data_val);
        }

        cJSON *item = cJSON_CreateString(print_buffer);
        cJSON_AddItemToArray(array, item);

        memset(print_buffer, 0, print_count);
        print_count = 0;
    }

    for (j = 0; j < remain; j++) {
        data_val = *(buffer + (lines << 4) + j);
        print_count += sprintf(print_buffer + print_count, "0x%02x ", data_val);

        cJSON *item = cJSON_CreateString(print_buffer);
        cJSON_AddItemToArray(array, item);
    }

}

void dump_data_to_json(cJSON* json, const char *const name, unsigned char *buffer, unsigned int num)
{
    dump_data_to_number_array_json(json, name, buffer, num);
}

void print_hex_with_buffer(unsigned char *buffer, unsigned int num, int print_level)
{
    unsigned char print_buffer[200] = {0};
    unsigned int print_count = 0;
    unsigned int lines, remain, i, j;
    unsigned char data_val;

    lines = num >> 4;
    remain = num & 0xF;

    for (i = 0; i < lines; i++) {
        for (j = 0; j < 16; j++) {
            data_val = *(buffer + (i << 4) + j);
            print_count += sprintf(print_buffer + print_count, "0x%02x ", data_val);
        }

        print_log_to_both("%s\"%s\",\n", print_level_tabel[print_level], print_buffer);

        memset(print_buffer, 0, print_count);
        print_count = 0;
    }

    for (j = 0; j < remain; j++) {
        data_val = *(buffer + (lines << 4) + j);
        print_count += sprintf(print_buffer + print_count, "0x%02x ", data_val);
    }

    print_log_to_both("%s\"%s\"\n", print_level_tabel[print_level], print_buffer);
}

void adler32(unsigned char data_val)
{
    unsigned int upper_word, lower_word;
    unsigned int base = 65521U;

    upper_word = (adler32_checksum >> 16) & 0xffff;
    lower_word = adler32_checksum & 0xffff;

    lower_word += data_val;
    if (lower_word >= base)
        lower_word -= base;

    upper_word += lower_word;

    if (upper_word >= base)
        upper_word -= base;

    adler32_checksum = lower_word | (upper_word << 16);
}
