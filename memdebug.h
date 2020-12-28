/***********/
/* Mutexes */
/***********/
#ifndef __INCLUDED_MUTEX
#define __INCLUDED_MUTEX

// All mutex functions return 0 on success

#ifdef _WIN32
// Use windows.h if compiling for Windows
#include <Windows.h>

#define mutex_t SRWLOCK
#define MUTEX_INITIALIZER SRWLOCK_INIT
static inline int mutex_init(mutex_t* mutex) {
    InitializeSRWLock(mutex);
    return 0;
}
static inline int mutex_lock(mutex_t* mutex) {
    AcquireSRWLockExclusive(mutex);
    return 0;
}
static inline int mutex_unlock(mutex_t* mutex) {
    ReleaseSRWLockExclusive(mutex);
    return 0;
}
static inline int mutex_destroy(mutex_t* mutex) { return 0; }

#else
// On other platforms use <pthread.h>
#include <pthread.h>

#define mutex_t pthread_mutex_t
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
static inline int mutex_init(mutex_t* mutex) { return pthread_mutex_init(mutex, NULL); }
static inline int mutex_lock(mutex_t* mutex) { return pthread_mutex_lock(mutex); }
static inline int mutex_unlock(mutex_t* mutex) { return pthread_mutex_unlock(mutex); }
static inline int mutex_destroy(mutex_t* mutex) { return pthread_mutex_destroy(mutex); }
#endif

#endif  // End mutex include guard

/************/
/* MEMDEBUG */
/************/
#ifndef __INCLUDED_MEMDEBUG
#define __INCLUDED_MEMDEBUG

#ifndef MEMDEBUG
#define MEMDEBUG 1
#endif

// PRINT_MEMALLOCS is used to control debug error messages for every allocation.
// Still wraps malloc() and tracks allocations for print_heap() if this is off.
#if MEMDEBUG
#ifndef PRINT_MEMALLOCS
#define PRINT_MEMALLOCS 1
#endif
#endif

#if MEMDEBUG
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/******************************************/
/* Void Pointer Hash Function For Hashmap */
/******************************************/

static inline size_t
log_base_2(const size_t num) {
    if (num == 1)
        return 0;
    return 1 + log_base_2(num / 2);
}

/* 
 * Note that, by all accounts, this is a bad idea. 
 * How ptr_hash behaves is entirely implementation specific because how uintptr_t is implementation specific. 
 * However, it behaves in the sane way that you'd expect across most popular compilers.
 */
#define MAP_BUF_SIZE 100000
static inline size_t
ptr_hash(void* val) {
    size_t logsize = log_base_2(1 + sizeof(void*));
    size_t shifted = (size_t)((uintptr_t)val) >> logsize;
    size_t other = (size_t)((uintptr_t)val) << (8 - logsize);
    size_t xed = shifted ^ other;
    size_t ans = xed % MAP_BUF_SIZE;
    return ans;
}

/**************************************/
/* Global Allocation Tracking Hashmap */
/**************************************/

// Mutex to guard the allocation structure.
static mutex_t alloc_mutex = MUTEX_INITIALIZER;
#define MEMDEBUG_LOCK_MUTEX mutex_lock(&alloc_mutex);
#define MEMDEBUG_UNLOCK_MUTEX mutex_unlock(&alloc_mutex);

struct MemAlloc;
typedef struct MemAlloc MemAlloc;
struct MemAlloc {
    void* ptr;
    size_t size;
    size_t line;
    const char* func;
    const char* file;
};

struct MapMember;
typedef struct MapMember MapMember;
struct MapMember {
    MemAlloc alloc;
    MapMember* next;
};

// Global alloc hash map
static MapMember allocs[MAP_BUF_SIZE];
static bool memallocs_initialized = false;
static size_t num_allocs = 0;

/***************/
/* Map Methods */
/***************/
static inline void
memallocs_init() {
    for (size_t i = 0; i < MAP_BUF_SIZE; i++) {
        allocs[i].alloc.ptr = NULL;
        allocs[i].next = NULL;
    }
    memallocs_initialized = true;
}

static inline void
alloc_add(MemAlloc alloc) {
    if (!memallocs_initialized)
        memallocs_init();

    num_allocs++;

    // If we can just insert into the map, do so.
    MapMember* bucket = (MapMember*)allocs + ptr_hash(alloc.ptr);

    // If we can insert into the main map array, do so
    if (bucket->alloc.ptr == NULL) {
        bucket->alloc = alloc;
        return;
    }

    // Otherwise, traverse the linked list until you find the end
    while (bucket->next != NULL) {
        bucket = bucket->next;
    }

    // Create a new LL node off the previous for the allocation
    bucket->next = (MapMember*)malloc(sizeof(MapMember));
    bucket = bucket->next;

    // Put the allocation into it.
    bucket->alloc = alloc;
    bucket->next = NULL;
}

// returns the pointer, or NULL if not found.
static inline bool
alloc_remove(void* ptr) {
    if (!memallocs_initialized)
        memallocs_init();

    MapMember* previous = NULL;
    MapMember* bucket = (MapMember*)allocs + ptr_hash(ptr);

    // Traverse the bucket looking for the pointer
    while (bucket) {
        if (bucket->alloc.ptr == ptr) {
            // Remove this bucket node from the linked list
            if (!previous) {
                // Copy from the next node into the original array.
                if (bucket->next) {
                    MapMember* to_free = bucket->next;
                    bucket->alloc = to_free->alloc;
                    bucket->next = to_free->next;
                    free(to_free);
                } else {
                    bucket->alloc.ptr = NULL;
                    bucket->next = NULL;
                }
            } else {
                // Point the previous allocation at the next allocation.
                // Then free the bucket.
                previous->next = bucket->next;
                free(bucket);
            }
            num_allocs--;
            return true;
        } else {
            previous = bucket;
            bucket = bucket->next;
        }
    }

    return false;
}

/**********************/
/* Externally visible */
/**********************/
extern void
print_heap() {
    size_t total_allocated = 0;

    MEMDEBUG_LOCK_MUTEX;

    if (!memallocs_initialized)
        memallocs_init();

    // For each bucket, traverse over each and print all the allocations
    printf("\n*************\n* HEAP DUMP *\n*************\n");
    for (size_t i = 0; i < MAP_BUF_SIZE; i++) {
        MapMember* bucket = (MapMember*)(allocs + i);
        while (bucket->alloc.ptr != NULL) {
            MemAlloc alloc = bucket->alloc;
            printf("Heap ptr: %p of size: %zu Allocated in file: %s On line: %zu\n",
                   alloc.ptr, alloc.size, alloc.file, alloc.line);
            total_allocated += alloc.size;

            if (bucket->next) {
                bucket = bucket->next;
            } else {
                break;
            }
        }
    }

    MEMDEBUG_UNLOCK_MUTEX;

    printf("\nTotal Heap size in bytes: %zu, number of items: %zu\n\n\n", total_allocated, num_allocs);
    fflush(stdout);
}

// At some point I may add callbacks to remedy this, but it shouldn't be too hard to just edit this file directly.
/**************************/
/* Not Externally visible */
/**************************/
#ifndef MEMPANIC_EXIT_STATUS
#define MEMPANIC_EXIT_STATUS 10
#endif
#ifndef OOM_EXIT_STATUS
#define OOM_EXIT_STATUS 11
#endif

static inline void
mempanic(void* badptr, const char* message, size_t line, const char* func, const char* file) {
    printf("MEMORY PANIC: %s\nPointer: %p\nOn line: %zu\nIn function: %s\nIn file: %s\nAborted.\n", message, badptr, line, func, file);
    fflush(stdout);
    exit(MEMPANIC_EXIT_STATUS);
}

static inline void
OOM(size_t line, const char* func, const char* file, size_t num_bytes) {
    printf("Out of memory on line %zu in %s() in file: %s.\nCould not allocate %zu bytes.\nDumping heap:\n", line, func, file, num_bytes);
    print_heap();
    exit(OOM_EXIT_STATUS);
}

/*********************************************/
/* malloc(), realloc(), free() Redefinitions */
/*********************************************/

extern void*
memdebug_malloc(size_t n, size_t line, const char* func, const char* file) {
    // Call malloc()
    void* ptr = malloc(n);
    if (!ptr) OOM(line, func, file, n);

#if PRINT_MEMALLOCS
    // Print message
    printf("malloc(%zu) -> %p on line %zu in %s() in %s.\n", n, ptr, line, func, file);
    fflush(stdout);
#endif

    // Keep a record of it
    MemAlloc newalloc;
    newalloc.ptr = ptr;
    newalloc.size = n;
    newalloc.line = line;
    newalloc.func = func;
    newalloc.file = file;

    MEMDEBUG_LOCK_MUTEX;
    alloc_add(newalloc);
    MEMDEBUG_UNLOCK_MUTEX;
    return ptr;
}

extern void*
memdebug_realloc(void* ptr, size_t n, size_t line, const char* func, const char* file) {
    MEMDEBUG_LOCK_MUTEX;

    // Check to make sure the allocation exists, and keep track of the location
    bool removed = alloc_remove(ptr);
    if (ptr != NULL && !removed) {
        mempanic(ptr, "Tried to realloc() an invalid pointer.", line, func, file);
    }

    // Call realloc()
    void* newptr = realloc(ptr, n);
    if (!newptr) OOM(line, func, file, n);

#if PRINT_MEMALLOCS
    // Print message
    printf("realloc(%p, %zu) -> %p on line %zu in %s() in %s.\n", ptr, n, newptr, line, func, file);
    fflush(stdout);
#endif

    // Update the record of allocations
    MemAlloc newalloc;
    newalloc.ptr = newptr;
    newalloc.size = n;
    newalloc.line = line;
    newalloc.func = func;
    newalloc.file = file;
    alloc_add(newalloc);

    MEMDEBUG_UNLOCK_MUTEX;

    return newptr;
}

extern void
memdebug_free(void* ptr, size_t line, const char* func, const char* file) {
    MEMDEBUG_LOCK_MUTEX;

    // Check to make sure the allocation exists, and keep track of the location
    bool removed = alloc_remove(ptr);
    if (ptr != NULL && !removed) {
        mempanic(ptr, "Tried to free() an invalid pointer.", line, func, file);
    }

    MEMDEBUG_UNLOCK_MUTEX;

    // Call free()
    free(ptr);

#if PRINT_MEMALLOCS
    // Print message
    printf("free(%p) on line %zu in %s() in %s.\n", ptr, line, func, file);
    fflush(stdout);
#endif
}

// Wrap malloc(), realloc(), free() with the new functionality

#define malloc(n) memdebug_malloc(n, __LINE__, __func__, __FILE__)
#define realloc(ptr, n) memdebug_realloc(ptr, n, __LINE__, __func__, __FILE__)
#define free(ptr) memdebug_free(ptr, __LINE__, __func__, __FILE__)

#else  // MEMDEBUG flag is disabled
/*************************************************************************************/
/* Define externally visible functions to do nothing when debugging flag is disabled */
/*************************************************************************************/

extern void print_heap() {}
#endif
#endif  // Include guard
