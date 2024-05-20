#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "cJSON.h"

extern FILE *compressed_data_log_file;
extern FILE *decompressed_data_log_file;
extern FILE *decompressed_data_file;

extern cJSON *compressed_data_json;
extern cJSON *decompressed_data_json;

extern char *print_level_tabel[];
extern unsigned char print_data_verbose;

extern unsigned int adler32_checksum;

void print_to_compressed_log(char *fmt, ...);
void print_to_decompressed_log(char *fmt, ...);

void print_compressed_data_hex(int data_val, cJSON* json);
void print_compressed_data_dec(int data_val, int print_level);

void print_decompressed_data_hex(int data_val, cJSON* json);

void dump_data_to_json(cJSON* json, const char *const name, unsigned char *buffer, unsigned int num);
void addStringToObjectFormatted(cJSON* json, const char *const name, const char *const format, ...);
void print_log_to_both(char *fmt, ...);
void print_hex_with_buffer(unsigned char *buffer, unsigned int num, int print_level);

unsigned int swap_uint32(unsigned int val);
void adler32(unsigned char data_val);

#endif