#!/usr/bin/env bash

cmake . && make clean && make

cp pass/eraser.so ./

cd lib && clang++ -O2 -D__LOCK_NUM_LIMIT=$1 -c library.cpp -emit-llvm -o rtLib.bc && cp rtLib.bc ../ && cd ..
