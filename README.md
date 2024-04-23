# Compressed File Editor
TODO Description...

## How to Use
TODO
1. build
2. in build 
3. some steps.

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