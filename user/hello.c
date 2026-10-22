static volatile unsigned long bss_value;
static volatile unsigned long data_value = 40;

static const char passed[] =
    "[user C] data and BSS test passed!\n";
static const char failed[] =
    "[user C] data and BSS test failed!\n";

static long syscall(long number, long argument) {
    long result;

    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "0"(number), "D"(argument)
        : "memory"
    );

    return result;
}

__attribute__((noreturn))
void _start(void) {
    bss_value = 2;

    if (data_value + bss_value == 42) {
        syscall(1, (long)passed);
    } else {
        syscall(1, (long)failed);
    }

    syscall(2, 0);

    for (;;) {
        __asm__ volatile ("pause");
    }
}
