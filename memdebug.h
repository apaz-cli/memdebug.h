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
int mutex_init(mutex_t* mutex) { InitializeSRWLock(mutex); return 0; }
int mutex_lock(mutex_t* mutex) { AcquireSRWLockExclusive(mutex); return 0; }
int mutex_unlock(mutex_t* mutex) { ReleaseSRWLockExclusive(mutex); return 0; }
int mutex_destroy(mutex_t* mutex) { return 0; }

#else
// On other platforms use <pthread.h>
#include <pthread.h>

#define mutex_t pthread_mutex_t
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
int mutex_init(mutex_t* mutex) { return pthread_mutex_init(mutex, NULL); }
int mutex_lock(mutex_t* mutex) { return pthread_mutex_lock(mutex); }
int mutex_unlock(mutex_t* mutex) { return pthread_mutex_unlock(mutex); }
int mutex_destroy(mutex_t* mutex) { return pthread_mutex_destroy(mutex); }
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
#include <stdio.h>
#include <stdlib.h>

/****************************************/
/* Global Allocation Tracking Variables */
/****************************************/

struct MemAlloc {
    void* ptr;
    size_t size;
    size_t line;
    char* file;
};
typedef struct MemAlloc MemAlloc;
#define MEMDEBUG_START_NUM_ALLOCS 5000
size_t num_allocs = 0;
size_t allocs_cap = 0;
MemAlloc* allocs;

// Mutex to guard the above allocation structure.
mutex_t alloc_mutex = MUTEX_INITIALIZER;
#define MEMDEBUG_LOCK_MUTEX mutex_lock(&alloc_mutex);
#define MEMDEBUG_UNLOCK_MUTEX mutex_unlock(&alloc_mutex);

#ifndef MEMPANIC_EXIT_STATUS
#define MEMPANIC_EXIT_STATUS 10
#endif
#ifndef OOM_EXIT_STATUS
#define OOM_EXIT_STATUS 11
#endif

/**********************/
/* Externally visible */
/**********************/
extern void print_heap();

// At some point I may add callbacks to remedy this, but it shouldn't be too hard to just edit this file directly.
/**************************/
/* Not Externally visible */
/**************************/
static inline void mempanic(void* badptr, char* message, size_t line, char* file);
static inline void OOM(size_t line, char* file, size_t num_bytes);

static inline void
mempanic(void* badptr, char* message, size_t line, char* file) {
    printf("MEMORY PANIC: %s\nPointer: %p\nOn line: %zu\nIn file: %s\nAborted.\n", message, badptr, line, file);
    fflush(stdout);
    exit(MEMPANIC_EXIT_STATUS);
}

static inline void
OOM(size_t line, char* file, size_t num_bytes) {
    printf("Out of memory on line %zu in file: %s.\nCould not allocate %zu bytes.\nDumping heap:\n", line, file, num_bytes);
    print_heap();
    exit(OOM_EXIT_STATUS);
}

extern void
print_heap() {
    size_t total_allocated = 0;
    size_t i;

    printf("\n*************\n* HEAP DUMP *\n*************\n");
    for (i = 0; i < num_allocs; i++) {
        printf("Heap ptr: %p of size: %zu Allocated in file: %s On line: %zu\n",
               allocs[i].ptr, allocs[i].size, allocs[i].file, allocs[i].line);
        total_allocated += allocs[i].size;
    }
    printf("\nTotal Heap size: %zu, number of items: %zu\n\n\n", total_allocated, num_allocs);
    fflush(stdout);
}

#define MEM_FAIL_TO_FIND 4294967295
static inline size_t
alloc_find_index(void* ptr) {
    size_t i;
    for (i = 0; i < num_allocs; i++) {
        if (allocs[i].ptr == ptr) {
            return i;
        }
    }
    return MEM_FAIL_TO_FIND;
}

static inline void
alloc_push(MemAlloc alloc) {
    MemAlloc* newptr;

    // Allocate more memory to store the information about the allocations if necessary
    if (num_allocs >= allocs_cap) {
        // If the list hasn't actually been initialized, initialize it. Otherwise, it needs to be grown.
        if (allocs_cap == 0) {
            allocs_cap = MEMDEBUG_START_NUM_ALLOCS;
            allocs = (MemAlloc*)malloc(sizeof(MemAlloc) * allocs_cap);
        } else {
            size_t new_allocs_cap = allocs_cap * 1.4;
            newptr = (MemAlloc*)realloc(allocs, sizeof(MemAlloc) * new_allocs_cap);
            if (!newptr) {
                printf("Failed to allocate more space to track allocations.\n");
                OOM(__LINE__, __FILE__, sizeof(MemAlloc) * new_allocs_cap);
            }
            allocs_cap = new_allocs_cap;
            allocs = newptr;
        }
    }

    // Append the new allocation to the list
    allocs[num_allocs] = alloc;
    num_allocs++;
}

static inline void
alloc_remove(size_t index) {
    // Shift the elements one left to remove it from the list.
    num_allocs--;
    for (; index < num_allocs; index++) {
        allocs[index] = allocs[index + 1];
    }
}

static inline void
alloc_update(size_t index, MemAlloc new_info) {
    // Update the entry in the list
    allocs[index] = new_info;
}

/*********************************************/
/* malloc(), realloc(), free() Redefinitions */
/*********************************************/

extern void*
memdebug_malloc(size_t n, size_t line, char* file) {
    // Call malloc()
    void* ptr = malloc(n);
    if (!ptr) OOM(line, file, n);

#if PRINT_MEMALLOCS
    // Print message
    printf("malloc(%zu) -> %p in %s, line %zu.\n", n, ptr, file, line);
    fflush(stdout);
#endif

    // Keep a record of it
    MemAlloc newalloc;
    newalloc.ptr = ptr;
    newalloc.size = n;
    newalloc.line = line;
    newalloc.file = file;

    MEMDEBUG_LOCK_MUTEX;
    alloc_push(newalloc);
    MEMDEBUG_UNLOCK_MUTEX;
    return ptr;
}

extern void*
memdebug_realloc(void* ptr, size_t n, size_t line, char* file) {
    MEMDEBUG_LOCK_MUTEX;

    // Check to make sure the allocation exists, and keep track of the location
    size_t alloc_index = alloc_find_index(ptr);
    if (ptr != NULL && alloc_index == MEM_FAIL_TO_FIND) {
        mempanic(ptr, "Tried to realloc() an invalid pointer.", line, file);
    }

    // Call realloc()
    void* newptr = realloc(ptr, n);
    if (!newptr) OOM(line, file, n);

#if PRINT_MEMALLOCS
    // Print message
    printf("realloc(%p, %zu) -> %p in %s, line %zu.\n", ptr, n, newptr, file, line);
    fflush(stdout);
#endif

    // Update the record of allocations
    MemAlloc newalloc;
    newalloc.ptr = newptr;
    newalloc.size = n;
    newalloc.line = line;
    newalloc.file = file;
    alloc_update(alloc_index, newalloc);

    MEMDEBUG_UNLOCK_MUTEX;

    return newptr;
}

extern void
memdebug_free(void* ptr, size_t line, char* file) {
    MEMDEBUG_LOCK_MUTEX;

    // Check to make sure the allocation exists, and keep track of the location
    size_t alloc_index = alloc_find_index(ptr);
    if (ptr != NULL && alloc_index == MEM_FAIL_TO_FIND) {
        mempanic(ptr, "Tried to free() an invalid pointer.", line, file);
    }

    // Call free()
    free(ptr);

#if PRINT_MEMALLOCS
    // Print message
    printf("free(%p) in %s, line %zu.\n", ptr, file, line);
    fflush(stdout);
#endif

    // Remove from the list of allocations
    alloc_remove(alloc_index);
    
    MEMDEBUG_UNLOCK_MUTEX;
}

// Wrap malloc(), realloc(), free() with the new functionality

#define malloc(n) memdebug_malloc(n, __LINE__, __FILE__)
#define realloc(ptr, n) memdebug_realloc(ptr, n, __LINE__, __FILE__)
#define free(ptr) memdebug_free(ptr, __LINE__, __FILE__)

#else  // MEMDEBUG flag is disabled
/*************************************************************************************/
/* Define externally visible functions to do nothing when debugging flag is disabled */
/*************************************************************************************/

void print_heap() {}
#endif
#endif  // Include guard
