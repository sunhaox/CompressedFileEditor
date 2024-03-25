#!/usr/bin/python3

import os
import sys
import json

byte_val = 0
bit_cnt = 0

def write_byte_array_2_file(byte_array, file_path):
    with open(file_path, "wb") as f:
        f.write(byte_array)

def construct_val_2_bits(byte_array, int_val, bit_size):
    global byte_val
    global bit_cnt
    for i in range(bit_size):
        byte_val |= ((int_val >> i) & 0x1) << bit_cnt
        bit_cnt += 1
        if (bit_cnt == 8):
            byte_array.append(byte_val)
            bit_cnt = 0
            byte_val = 0

def construct_stream_2_bits(byte_array, int_val, bit_size):
    global byte_val
    global bit_cnt
    for i in range(bit_size):
        byte_val |= ((int_val >> (bit_size - 1 - i)) & 0x1) << bit_cnt
        bit_cnt += 1
        if (bit_cnt == 8):
            byte_array.append(byte_val)
            bit_cnt = 0
            byte_val = 0

def fill_reserved_bits_with_0(byte_array):
    global byte_val
    global bit_cnt
    if bit_cnt != 0:
        for i in range(8 - bit_cnt):
            byte_val |= 0 << bit_cnt
            bit_cnt += 1
            if (bit_cnt == 8):
                byte_array.append(byte_val)
                bit_cnt = 0
                byte_val = 0

def flush_final(byte_array):
    global byte_val
    global bit_cnt
    if bit_cnt != 0:
        byte_array.append(byte_val)
        bit_cnt = 0
        byte_val = 0

def construct_deflate_header(json_data, byte_array):
    construct_val_2_bits(byte_array, json_data["BFINAL"]["value"],
        json_data["BFINAL"]["bit_size"])
    construct_val_2_bits(byte_array, json_data["BTYPE"]["stored_value"],
        json_data["BTYPE"]["bit_size"])

def construct_deflate_stored_block(json_data, byte_array):
    fill_reserved_bits_with_0(byte_array)
    construct_val_2_bits(byte_array, json_data["LEN"]["value"],
        json_data["LEN"]["bit_size"])
    construct_val_2_bits(byte_array, json_data["NLEN"]["value"],
        json_data["NLEN"]["bit_size"])

    raw_data_list = []
    for raw_data_str in json_data["RAW_DATA"]:
        symbol_list = list(filter(lambda x : x, raw_data_str.split(" ")))
        raw_data_list += symbol_list

    if raw_data_list:
        for raw_data in raw_data_list:
            byte_array.append(int(raw_data, 16))

def construct_deflate_fix_block(json_data, byte_array):
    length_extra_bits_table = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0]

    distance_extra_bits_table = [
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13]

    literal_length_huffman_table = []
    distance_huffman_table = []
    literal_length_symbol_dict = {}
    distance_symbol_dict = {}

    literal_length_huffman_table = json_data["extracted_literal_length_huffman_table"]["items"]
    for items in literal_length_huffman_table:
        literal_length_symbol_dict[items["symbol_value"]] = items["index"]

    if "items" in json_data["extracted_distance_huffman_table"]:
        distance_huffman_table = json_data["extracted_distance_huffman_table"]["items"]
        for items in distance_huffman_table:
            distance_symbol_dict[items["symbol_value"]] = items["index"]

    decoded_symbol_list = []
    for symbol_str in json_data["ENCODED_BIT_STREAM"]:
        symbol_list = list(filter(lambda x : x, symbol_str.split(" ")))
        decoded_symbol_list += symbol_list

    symbol_list_length = len(decoded_symbol_list)
    symbol_list_index = 0
    while (symbol_list_index < symbol_list_length):
        if "0x" in decoded_symbol_list[symbol_list_index]:
            symbol_value = int(decoded_symbol_list[symbol_list_index], 16)
            huffman_table_index = literal_length_symbol_dict[symbol_value]
            construct_stream_2_bits(byte_array,
                literal_length_huffman_table[huffman_table_index]["encoded_value"],
                literal_length_huffman_table[huffman_table_index]["encoded_bit_size"])
            symbol_list_index += 1
        elif "256" == decoded_symbol_list[symbol_list_index]:
            symbol_value = int(decoded_symbol_list[symbol_list_index])
            huffman_table_index = literal_length_symbol_dict[symbol_value]
            construct_stream_2_bits(byte_array,
                literal_length_huffman_table[huffman_table_index]["encoded_value"],
                literal_length_huffman_table[huffman_table_index]["encoded_bit_size"])
            symbol_list_index += 1
        else:
            length_value = int(decoded_symbol_list[symbol_list_index])
            huffman_table_index = literal_length_symbol_dict[length_value]
            construct_stream_2_bits(byte_array,
                literal_length_huffman_table[huffman_table_index]["encoded_value"],
                literal_length_huffman_table[huffman_table_index]["encoded_bit_size"])

            length_extra_value = int(decoded_symbol_list[symbol_list_index + 1])
            length_extra_bits = length_extra_bits_table[length_value - 257]
            construct_val_2_bits(byte_array, length_extra_value,
            length_extra_bits)

            distance_value = int(decoded_symbol_list[symbol_list_index + 2])
            huffman_table_index = distance_symbol_dict[distance_value]
            construct_stream_2_bits(byte_array,
                distance_huffman_table[huffman_table_index]["encoded_value"],
                distance_huffman_table[huffman_table_index]["encoded_bit_size"])

            distance_extra_value = int(decoded_symbol_list[symbol_list_index + 3])
            distance_extra_bits = distance_extra_bits_table[distance_value]
            construct_val_2_bits(byte_array, distance_extra_value,
            distance_extra_bits)

            symbol_list_index += 4

def construct_deflate_dynamic_block(json_data, byte_array):
    length_extra_bits_table = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0]

    distance_extra_bits_table = [
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13]

    literal_length_huffman_table = []
    distance_huffman_table = []
    literal_length_symbol_dict = {}
    distance_symbol_dict = {}

    construct_val_2_bits(byte_array, json_data["HLIT"]["stored_value"],
        json_data["HLIT"]["bit_size"])
    construct_val_2_bits(byte_array, json_data["HDIST"]["stored_value"],
        json_data["HDIST"]["bit_size"])
    construct_val_2_bits(byte_array, json_data["HCLEN"]["stored_value"],
        json_data["HCLEN"]["bit_size"])

    for code_length in json_data["CODE_LENGTH_TABLE"]:
        if code_length["stored"]:
            construct_val_2_bits(byte_array, code_length["stored_value"],
            code_length["bit_size"])

    for literal_length_distance in json_data["LITERAL_LENGTH_DISTANCE_TABLE"]:
        construct_stream_2_bits(byte_array, literal_length_distance["stored_value"],
        literal_length_distance["bit_size"])
        if "extra" in literal_length_distance:
            construct_val_2_bits(byte_array, literal_length_distance["extra"]["stored_value"],
            literal_length_distance["extra"]["bit_size"])

    literal_length_huffman_table = json_data["extracted_literal_length_huffman_table"]["items"]
    for items in literal_length_huffman_table:
        literal_length_symbol_dict[items["symbol_value"]] = items["index"]

    if "items" in json_data["extracted_distance_huffman_table"]:
        distance_huffman_table = json_data["extracted_distance_huffman_table"]["items"]
        for items in distance_huffman_table:
            distance_symbol_dict[items["symbol_value"]] = items["index"]

    decoded_symbol_list = []
    for symbol_str in json_data["ENCODED_BIT_STREAM"]:
        symbol_list = list(filter(lambda x : x, symbol_str.split(" ")))
        decoded_symbol_list += symbol_list

    symbol_list_length = len(decoded_symbol_list)
    symbol_list_index = 0
    while (symbol_list_index < symbol_list_length):
        if "0x" in decoded_symbol_list[symbol_list_index]:
            symbol_value = int(decoded_symbol_list[symbol_list_index], 16)
            huffman_table_index = literal_length_symbol_dict[symbol_value]
            construct_stream_2_bits(byte_array,
                literal_length_huffman_table[huffman_table_index]["encoded_value"],
                literal_length_huffman_table[huffman_table_index]["encoded_bit_size"])
            symbol_list_index += 1
        elif "256" == decoded_symbol_list[symbol_list_index]:
            symbol_value = int(decoded_symbol_list[symbol_list_index])
            huffman_table_index = literal_length_symbol_dict[symbol_value]
            construct_stream_2_bits(byte_array,
                literal_length_huffman_table[huffman_table_index]["encoded_value"],
                literal_length_huffman_table[huffman_table_index]["encoded_bit_size"])
            symbol_list_index += 1
        else:
            length_value = int(decoded_symbol_list[symbol_list_index])
            huffman_table_index = literal_length_symbol_dict[length_value]
            construct_stream_2_bits(byte_array,
                literal_length_huffman_table[huffman_table_index]["encoded_value"],
                literal_length_huffman_table[huffman_table_index]["encoded_bit_size"])

            length_extra_value = int(decoded_symbol_list[symbol_list_index + 1])
            length_extra_bits = length_extra_bits_table[length_value - 257]
            construct_val_2_bits(byte_array, length_extra_value,
            length_extra_bits)

            distance_value = int(decoded_symbol_list[symbol_list_index + 2])
            huffman_table_index = distance_symbol_dict[distance_value]
            construct_stream_2_bits(byte_array,
                distance_huffman_table[huffman_table_index]["encoded_value"],
                distance_huffman_table[huffman_table_index]["encoded_bit_size"])

            distance_extra_value = int(decoded_symbol_list[symbol_list_index + 3])
            distance_extra_bits = distance_extra_bits_table[distance_value]
            construct_val_2_bits(byte_array, distance_extra_value,
            distance_extra_bits)

            symbol_list_index += 4

def construct_deflate_bin(json_data, byte_array):
    for deflate_block in json_data:
        construct_deflate_header(deflate_block, byte_array)
        if deflate_block["BTYPE"]["stored_value"] == 0:
            construct_deflate_stored_block(deflate_block, byte_array)
        elif deflate_block["BTYPE"]["stored_value"] == 1:
            construct_deflate_fix_block(deflate_block, byte_array)
        elif deflate_block["BTYPE"]["stored_value"] == 2:
            construct_deflate_dynamic_block(deflate_block, byte_array)

    flush_final(byte_array)

def construct_zlib_header(json_data, byte_array):
    construct_val_2_bits(byte_array, json_data["COMPRESSION_METHOD"]["value"],
        json_data["COMPRESSION_METHOD"]["bit_size"])
    construct_val_2_bits(byte_array, json_data["COMPRESSION_INFO"]["value"],
        json_data["COMPRESSION_INFO"]["bit_size"])

    construct_val_2_bits(byte_array, json_data["FLAGS"]["FCHECK"]["value"],
        json_data["FLAGS"]["FCHECK"]["bit_size"])
    construct_val_2_bits(byte_array, json_data["FLAGS"]["FDICT"]["value"],
        json_data["FLAGS"]["FDICT"]["bit_size"])
    construct_val_2_bits(byte_array, json_data["FLAGS"]["FLEVEL"]["value"],
        json_data["FLAGS"]["FLEVEL"]["bit_size"])

def construct_zlib_footer(json_data, byte_array):
    for checksum_str in json_data["value"]:
        checksum_list = list(filter(lambda x : x, checksum_str.split(" ")))
        for checksum_val in checksum_list:
            byte_array.append(int(checksum_val, 16))

def construct_zlib_bin(json_data, byte_array):
    construct_zlib_header(json_data["ZLIB_HEADER"], byte_array)
    construct_deflate_bin(json_data["DEFLATE_BLOCK"], byte_array)
    construct_zlib_footer(json_data["CHECKSUM_IN_FILE"], byte_array)

def construct_compressed_bin(json_data, output_file_path):
    output_file_name = output_file_path
    compressed_byte_array = bytearray()
    if "ZLIB_FORMAT" in json_data:
        construct_zlib_bin(json_data["ZLIB_FORMAT"], compressed_byte_array)
        output_file_name += ".zlib"
    elif "DEFLATE_BLOCK" in json_data:
        construct_deflate_bin(json_data["DEFLATE_BLOCK"], compressed_byte_array)
        output_file_name += ".defl"

    write_byte_array_2_file(compressed_byte_array, output_file_name)

def main():
    if len(sys.argv) < 2:
        sys.exit(0)

    compressed_json_file = sys.argv[1]
    if not os.path.exists(compressed_json_file):
        print(compressed_json_file + " not exists!")
        sys.exit(0)

    with open(compressed_json_file) as f:
        json_data = json.load(f)
        construct_compressed_bin(json_data, compressed_json_file)

if __name__ == "__main__":
    main()