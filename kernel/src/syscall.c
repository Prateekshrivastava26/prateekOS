#include <stdint.h>
#include "console.h"

uint64_t syscall_dispatch(uint64_t number, uint64_t argument) {
    if (number == 1) {
        /* Demo syscall: print a string supplied by this test program. */
        console_write((const char *)(uintptr_t)argument);
        return 0;
    }

    if (number == 2) {
        /* Tell the assembly handler to return to the kernel caller. */
        return 1;
    }

    return 0;
}
