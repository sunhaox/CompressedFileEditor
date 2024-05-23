# Compressed File Editor
This is a tool can help you to dump and understand the compressed file format. It can help you to print the file structure to JSON.

## How to Use
There are 5 executable files for deflate, gzip, zlib, lz4 and zstd compressed file.  
Use following command to generate the structure JSON.
```
./deflate_dump <compressed_file>
./gzip_dump <compressed_file>
./zlib_dump <compressed_file>
./lz4_dump <compressed_file>
./zstd_dump <compressed_file>
```


## How to Build
### Linux
```
mkdir build
cd build
cmake ..
make
```
All the output files will be in `output` folder.  

### Windows
Use the Visual Studio as the compiler.  
```
mkdir build
cd build
cmake ..
```
Then you can open the `CompressedFileEditor.sln` to edit and compile the code.

## How to Debug
### Linux
Use this command to generate the makefile to build with debug symbols:
```
cmake -DCMAKE_BUILD_TYPE=Debug ..
```
Then build the project.

### Windows
Open the `CompressedFileEditor.sln` using Visual Studio and change the **Solution Configurations** to **Debug**. Rebuild the solution or only one project and start debugging.

## Thanks
[cJSON](https://github.com/DaveGamble/cJSON): Ultralightweight JSON parser in ANSI C.  
puff: deflate implementation - from the zlib library and created by Mark Adler  
[LZ4](https://github.com/lz4/lz4): Extremely Fast Compression algorithm.  
[zstd](https://github.com/facebook/zstd): Fast real-time compression algorithm from Meta.