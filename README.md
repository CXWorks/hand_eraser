# hand_eraser

The C++ and LLVM implementation of Eraser data race detector. [[paper](https://dl.acm.org/doi/10.1145/265924.265927)]

## Dependency

+ LLVM 6.0 + 
+ clang/clang++ 6.0 +
+ CMake 3.10 +

## Usage

This implementation contains 2 parts: in the lib folder is a runtime library and pass folder is a LLVM pass to 
do the instrumentation. 

1. To build library, goto lib folder and run, **lock_num** is the number of locks in your program:

        clang++ -O2 -emit-llvm -D__LOCK_NUM_LIMIT=${lock_num} -c library.cpp

2. To build LLVM pass, goto root folder:

        cmake . && make

3. build the executable program(support target program is sort):

        llvm-link sort.bc library.bc -o combined.bc
        opt -load ${project}/pass/eraser.so -eraser combined.bc -o inst.bc
        clang++ -O2 inst.bc -lpthread
        ./a.out