/*
 * Copyright (C) 2019 Xiang Cheng
 */
#ifndef ERASERLIB_LIBRARY_H
#define ERASERLIB_LIBRARY_H
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <map>
#include <cassert>
#include <list>
#include <climits>
#include <atomic>
#include "PageMap.h"

#define __NUM_OF_LOCKS (1<<20)
typedef  unsigned char __TYPE_OF_LOCK;
#define  MAX_VAL (UCHAR_MAX);
using namespace std;
using namespace sync_p;


//This is the struct for thead lock => records each thread's lock
typedef struct ThreadLock{
    int id;
    __TYPE_OF_LOCK lockSet[__LOCK_NUM_LIMIT];
    __TYPE_OF_LOCK wlockSet[__LOCK_NUM_LIMIT];
} ThreadLock;

ThreadLock* initThreadLock(){
    auto * answer = static_cast<ThreadLock *>(malloc(sizeof(struct ThreadLock)));
    if(answer != nullptr){
        answer->id = -1;
        for (__TYPE_OF_LOCK &i : answer->lockSet) {
            i = 0;
        }
        for (__TYPE_OF_LOCK &i : answer->wlockSet) {
            i = 0;
        }
    } else
        printf("malloc failed\n");
    return answer;
}

//Main class for each memory access location
//Here transfer all pointer to void*, which is uintptr_t
class MemPointer{
public:
    unsigned char status = 0;
    unsigned char access = 0;
    __TYPE_OF_LOCK lockSet[__LOCK_NUM_LIMIT];
    MemPointer(){
        status = 0;
        for (__TYPE_OF_LOCK &i : this->lockSet) {
            i = MAX_VAL;
        }
        access = 0;
    }


} ;



//function define
ThreadLock& findThread(pthread_t pid);
unsigned int findLockId(uintptr_t location);
void printMP(MemPointer* mp);
#endif