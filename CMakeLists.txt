cmake_minimum_required(VERSION 3.1)

project(CompressedFileEditor VERSION 1.0)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)


add_executable(gzip_dump gzip_dump.c puff.c utils.c)
add_executable(zlib_dump zlib_dump.c puff.c utils.c)
add_executable(deflate_dump deflate_dump.c puff.c utils.c)
add_executable(lz4_dump lz4_dump.c puff.c utils.c)
add_executable(zstd_dump zstd_dump.c puff.c utils.c)
