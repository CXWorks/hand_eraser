/*
 * Copyright (C) 2019 Xiang Cheng
 */
#include "library.h"

//global lock id to allocate each locks location in bit vector
static unsigned long lockCount = 0;

static pthread_spinlock_t countLock;

static PageMap<PageSizes<20, 16, 9>, uintptr_t, MemPointer> *memPointerMap;

static map<uintptr_t, uint> *lockAllocationTableMap;

static map<uintptr_t, size_t> *allocMap;

static atomic_int thread_counter = {0};

static thread_local void *g_key = NULL;

static pthread_spinlock_t mpLock;

static pthread_spinlock_t mallocLock;

static pthread_spinlock_t mem_locks[__NUM_OF_LOCKS];

static bool inited = false;


//This function init all gloabl variables
//Inject in the first instruction of main
void initEraser() {
    if (!inited) {
        pthread_spin_init(&countLock, 0);
        pthread_spin_init(&mpLock, 0);
        pthread_spin_init(&mallocLock, 0);
        for (int i = 0; i < __NUM_OF_LOCKS; ++i) {
            pthread_spin_init(&mem_locks[i], 0);
        }

        memPointerMap = new PageMap<PageSizes<20, 16, 9>, unsigned long, MemPointer>();
        lockAllocationTableMap = new map<uintptr_t, uint>();
        allocMap = new map<uintptr_t, size_t>();
        inited = true;
        printf("init eraser\n");
    }
}


//Inject before the end of main
void destroyEraser() {
    printf("end --------------------- %d\n", thread_counter.load());
}


//find / allocate the lock id(start from 0), also the location in bit vector
unsigned int findLockId(uintptr_t location) {


    auto it = lockAllocationTableMap->find(location);

    if (it != lockAllocationTableMap->end()) {
        return it->second;
    } else {
        //critical section
        pthread_spin_lock(&countLock);
        auto it = lockAllocationTableMap->find(location);
        if (it != lockAllocationTableMap->end()) {
            pthread_spin_unlock(&countLock);
            return (it->second);
        }
        uint id = lockCount++;
        (*lockAllocationTableMap)[location] = id;
        pthread_spin_unlock(&countLock);
        //end critical section
        return id;
    }

}


//Find/allocate the memory pointer
//The bottle neck maybe
MemPointer& findMemPointer(uintptr_t location) {

    return (memPointerMap->get(location >> 3));
}


//Only find, don't allocate
MemPointer *findMPNoCreate(uintptr_t location) {
    pair<int, MemPointer *> ans = memPointerMap->test(location >> 3);
    if (ans.second != nullptr) {
        return ans.second;
    } else
        return nullptr;
}


//find / allocate thread
ThreadLock& findThread(pthread_t pid) {

    if (g_key == NULL) {
        ThreadLock *t = initThreadLock();
        t->id = thread_counter++;
        g_key = t;
    }
    return *((ThreadLock *) g_key);
}

//0 for lock, 1 for unlock
//inject before/after release/get lock
void lockAccess(int type, void *locat) {

    uintptr_t location = (uintptr_t) locat;
    unsigned long id = findLockId(location);
    assert(id < (sizeof(__TYPE_OF_LOCK) * 8 * (__LOCK_NUM_LIMIT)));
    pthread_t pid = pthread_self();
    ThreadLock& tl = findThread(pid);
    unsigned long quotient = id / (sizeof(__TYPE_OF_LOCK) * 8);
    unsigned long remainder = id % (sizeof(__TYPE_OF_LOCK) * 8);
    unsigned char mask = 1 << remainder;
    if (type == 0) {
        tl.lockSet[quotient] |= mask;
        tl.wlockSet[quotient] |= mask;
//            assert(tl->lockSet[quotient] != 0);
    } else {
        mask = ~mask;
        tl.lockSet[quotient] &= mask;
        tl.wlockSet[quotient] &= mask;
    }

}


void lockRAccess(int type, void *locat) {

    uintptr_t location = (uintptr_t) locat;
    unsigned long id = findLockId(location);
    assert(id < (sizeof(__TYPE_OF_LOCK) * 8 * (__LOCK_NUM_LIMIT)));
    pthread_t pid = pthread_self();
    ThreadLock& tl = findThread(pid);
    unsigned long quotient = id / (sizeof(__TYPE_OF_LOCK) * 8);
    unsigned long remainder = id % (sizeof(__TYPE_OF_LOCK) * 8);
    unsigned char mask = 1 << remainder;
    if (type == 0) {
        tl.lockSet[quotient] |= mask;
//            assert(tl->lockSet[quotient] != 0);
    } else {
        mask = ~mask;
        tl.lockSet[quotient] &= mask;
    }

}


inline unsigned long get_addr(unsigned long long key) {
    return (key >> 3) & (__NUM_OF_LOCKS - 1);
}


static u_char qtable[]{0, 1, 2, 3};
static u_char wtable[]{1, 3, 3, 3};
static u_char rtable[]{0, 2, 2, 3};

// 0 for write, 1 for read
//inject after load/store inst
void memoryAccess(int type, void *locat) {
    unsigned long long location = ((uintptr_t) locat);
    pthread_t pid = pthread_self();
    ThreadLock& tl = findThread(pid);
    int idx = tl.id;
    unsigned char caccess = 1 << idx;
    if (type == 0) {
        MemPointer& mp = findMemPointer(location);
        pthread_spin_lock(&mem_locks[get_addr(location)]);
        switch ((mp.access & caccess)) {
            case 0:
                mp.access |= caccess;
                mp.status = wtable[mp.status];
                break;
            default:

                mp.status = qtable[mp.status];

        }

        switch (mp.status) {
            case 0:

            case 1:
                break;
            case 2:
            case 3:
                for (int i = 0; i < __LOCK_NUM_LIMIT; i++) {
                    mp.lockSet[i] &= tl.wlockSet[i];
                }
                break;
        }

        pthread_spin_unlock(&mem_locks[get_addr(location)]);
    } else {
        MemPointer& mp = findMemPointer(location);
        pthread_spin_lock(&mem_locks[get_addr(location)]);
        if (mp.status != 0) {
            switch (mp.access & caccess) {
                case 0:
                    mp.status = rtable[mp.status];
                    mp.access |= caccess;
            }
            switch (mp.status) {
                case 0:

                case 1:
                    break;
                case 2:
                case 3:
                    for (int i = 0; i < __LOCK_NUM_LIMIT; i++) {
                        mp.lockSet[i] &= tl.lockSet[i];
                    }
                    break;
            }
        }
        pthread_spin_unlock(&mem_locks[get_addr(location)]);
    }


}



void onMalloc(void *locat, size_t size) {
    unsigned long long location = ((uintptr_t) locat);
    pthread_spin_lock(&mallocLock);
    (*allocMap)[location] = size;
    pthread_spin_unlock(&mallocLock);
}

void onFree(void *locat) {
    unsigned long long location = ((uintptr_t) locat);
    pthread_spin_lock(&mallocLock);
    size_t size = (*allocMap)[location];
    allocMap->erase(location);
    pthread_spin_unlock(&mallocLock);
    for (unsigned long i = 0; i < size; i += 4) {
        if (memPointerMap->test(location + i).first) {
            MemPointer &mp = *findMPNoCreate(location + i);
            mp.status = 0;
            for (__TYPE_OF_LOCK &i : mp.lockSet) {
                i = MAX_VAL;
            }
            mp.access = 0;
        }
    }
}