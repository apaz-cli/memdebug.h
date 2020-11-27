# memdebug.h
A drop-in replacement for malloc that overrides malloc(), realloc(), and free() for the remainder of the translation unit for easy memory debugging.


# Example usage: 
```c
#include <stdio.h>
#include <stdlib.h>

// #define MEMDEBUG to zero to disable all wrapping and features.
// No allocations will be tracked, and print_heap() and any additional debugging
// features that are added will be made to do nothing.

// #define MEMDEBUG 0
#include "memdebug.h"

int main() {
    // Print debug messages on allocation/free
    void* ptr = malloc(1);
    ptr = realloc(ptr, 10);
    free(ptr);

    // Find memory leaks
    malloc(20);
    malloc(25);
    print_heap();

    // Explode gracefully
    free(NULL);
}
```

## Output
```
apaz@apaz-laptop:~/git/memdebug$ gcc test.c
apaz@apaz-laptop:~/git/memdebug$ ./a.out
malloc(1) -> 0x55c76ccd1260 in test.c, line 13.
realloc(0x55c76ccd1260, 10) -> 0x55c76ccd1260 in test.c, line 14.
free(0x55c76ccd1260) in test.c, line 15.
malloc(20) -> 0x55c76ccd1260 in test.c, line 18.
malloc(25) -> 0x55c76ccd1690 in test.c, line 19.

*************
* HEAP DUMP *
*************
Heap ptr: 0x55c76ccd1260 of size: 20 Allocated in file: test.c On line: 18
Heap ptr: 0x55c76ccd1690 of size: 25 Allocated in file: test.c On line: 19

Total Heap size: 45, number of items: 2


MEMORY PANIC: Tried to free() an invalid pointer.
Pointer: (nil)
On line: 23
In file: test.c
Aborted.
```
