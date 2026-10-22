#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "arch/x86_64/idt.h"
#include "arch/x86_64/gdt.h"
#include "timer.h"
#include "keyboard.h"
#include "console.h"
#include "gui.h"
#include "mouse.h"
#include "pmm.h"
#include "heap.h"
#include "vmm.h"
#include "task.h"
#include "user.h"
#include "ramfs.h"
#include "ide.h"
#include "diskfs.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static uint32_t fb_channel(uint8_t value, uint8_t mask_size, uint8_t mask_shift) {
    uint64_t max = ((uint64_t)1 << mask_size) - 1;
    return (uint32_t)((value * max / 255) << mask_shift);
}

static uint32_t fb_pixel(
    struct limine_framebuffer *fb,
    uint8_t red,
    uint8_t green,
    uint8_t blue
) {
    return fb_channel(red, fb->red_mask_size, fb->red_mask_shift)
         | fb_channel(green, fb->green_mask_size, fb->green_mask_shift)
         | fb_channel(blue, fb->blue_mask_size, fb->blue_mask_shift);
}

static void fb_pattern(struct limine_framebuffer *fb) {
    volatile uint32_t *fb_ptr = fb->address;

    for (size_t y = 0; y < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            uint8_t blue = (uint8_t)(x * 255 / fb->width);
            uint8_t green = (uint8_t)(y * 255 / fb->height);

            fb_ptr[y * (fb->pitch / 4) + x] =
                fb_pixel(fb, 0, green, blue);
        }
    }
}


static const char keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static int command_matches(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return 0;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static void console_write_u64(uint64_t value) {
    char digits[20];
    size_t count = 0;

    if (value == 0) {
        console_putc('0');
        return;
    }

    while (value > 0) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (count > 0) {
        console_putc(digits[--count]);
    }
}

static void *test_allocated_page = NULL;
static int return_to_desktop;

static int run_user_module(void) {
    if (module_request.response == NULL ||
        module_request.response->module_count == 0 ||
        module_request.response->modules == NULL ||
        module_request.response->modules[0] == NULL) {
        console_write("No user ELF module is available\n");
        return 0;
    }

    struct limine_file *module =
        module_request.response->modules[0];
    return user_test_run(module->address, module->size);
}

static int command_starts_with(const char *command,
                                 const char *prefix) {
    while (*prefix != '\0') {
        if (*command++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static void run_command(const char *command) {
    if (command_matches(command, "desktop")) {
        return_to_desktop = 1;
    } else if (command_matches(command, "help")) {
        console_write("Commands: help, clear, about, desktop, ls, cat <path>, write <path> <text>, rm <path>, disktest, diskformat, diskwrite <name> <text>, diskcat <name>, diskls, diskrm <name>, cheems <program-path>\n");
    } else if (command_matches(command, "clear")) {
        console_clear();
    } else if (command_matches(command, "about")) {
        console_write("PRATEEKOS: a small learning operating system\n");
    } else if (command_matches(command, "mem")) {
        console_write("Total pages: ");
        console_write_u64(pmm_total_page_count());
        console_write("\nFree pages:  ");
        console_write_u64(pmm_free_page_count());
        console_write("\nFree memory: ");
        console_write_u64(
            (pmm_free_page_count() * PMM_PAGE_SIZE) / (1024 * 1024)
        );
        console_write(" MiB\n");
    } else if (command_matches(command, "alloc")) {
        if (test_allocated_page != NULL) {
            console_write("A test page is already allocated. Type free first.\n");
        } else {
            test_allocated_page = pmm_alloc_page();

            if (test_allocated_page == NULL) {
                console_write("Page allocation failed.\n");
            } else {
                console_write("Allocated one 4 KiB page.\n");
            }
        }
    } else if (command_matches(command, "free")) {
        if (test_allocated_page == NULL) {
            console_write("No test page is allocated. Type alloc first.\n");
        } else {
            pmm_free_page(test_allocated_page);
            test_allocated_page = NULL;
            console_write("Freed the test page.\n");
        }
    } else if (command_matches(command, "heaptest")) {
        const char message[] = "Heap allocation passed.";
        char *memory = kmalloc(sizeof(message));

        if (memory == NULL) {
            console_write("Heap allocation failed.\n");
        } else {
            for (size_t i = 0; i < sizeof(message); i++) {
                memory[i] = message[i];
            }

            console_write(memory);
            console_putc('\n');
            kfree(memory);
            console_write("Heap page freed.\n");
        }
    } else if (command_matches(command, "vmtest")) {
        if (vmm_self_test()) {
            console_write("Virtual page map/write/unmap test passed.\n");
        } else {
            console_write("Virtual memory test failed.\n");
        }
    } else if (command_matches(command, "tasktest")) {
        console_write("Starting cooperative tasks...\n");
        if (task_test()) {
            console_write("Task test finished.\n");
        } else {
            console_write("Task test could not start.\n");
        }
    } else if (command_matches(command, "usertest")) {
        console_write("Entering ring 3 user mode...\n");
        if (run_user_module()) {
            console_write("Returned to the kernel successfully.\n");
        } else {
            console_write("User-mode test failed.\n");
        }
    } else if (command_starts_with(command, "cheems ")) {
        const uint8_t *program;
        uint64_t program_size;

        if (!ramfs_get_file(command + 7, &program, &program_size)) {
            console_write("Program file not found\n");
        } else if (user_test_run(program, program_size)) {
            console_write("Program returned to PRATEEKOS\n");
        } else {
            console_write("Program load failed\n");
        }
    } else if (command_matches(command, "disktest")) {
        int result = ide_persistence_test();

        if (result < 0) {
            console_write("Disk test failed. Check the QEMU disk attachment.\n");
        } else if (result == 0) {
            console_write("Disk works; marker saved. Restart QEMU, then run disktest again.\n");
        } else {
            console_write("Persistent disk test passed. Marker survived the restart.\n");
        }
    } else if (command_matches(command, "diskformat")) {
        if (diskfs_format()) {
            console_write("Disk filesystem formatted.\n");
        } else {
            console_write("Disk format failed. Check the attached disk.\n");
        }
    } else if (command_matches(command, "diskls")) {
        diskfs_list();
    } else if (command_starts_with(command, "diskwrite ")) {
        char *arguments = (char *)command + 10;
        char *separator = arguments;

        while (*separator != '\0' && *separator != ' ') {
            separator++;
        }

        if (*separator == '\0') {
            console_write("Usage: diskwrite <name> <text>\n");
        } else {
            *separator = '\0';
            if (diskfs_write_file(arguments, separator + 1)) {
                console_write("File saved to disk.\n");
            } else {
                console_write("Disk write failed. Format the disk or check limits.\n");
            }
        }
    } else if (command_starts_with(command, "diskcat ")) {
        if (diskfs_cat(command + 8)) {
            console_putc('\n');
        } else {
            console_write("Disk file not found or disk is not formatted.\n");
        }
    } else if (command_starts_with(command, "diskrm ")) {
        if (diskfs_remove_file(command + 7)) {
            console_write("Disk file removed.\n");
        } else {
            console_write("Disk file not found or disk is not formatted.\n");
        }
    } else if (command_starts_with(command, "rm ")) {
        if (ramfs_remove_file(command + 3)) {
            console_write("Removed RAM file\n");
        } else {
            console_write("RAM file not found\n");
        }
    } else if (command_starts_with(command, "write ")) {
        char *arguments = (char *)command + 6;
        char *separator = arguments;

        while (*separator != '\0' && *separator != ' ') {
            separator++;
        }

        if (*separator == '\0') {
            console_write("Usage: write <path> <text>\n");
        } else {
            *separator = '\0';
            const char *text_to_write = separator + 1;

            if (ramfs_write_file(arguments, text_to_write)) {
                console_write("File written in RAM: ");
                console_write(arguments);
                console_putc('\n');
            } else {
                console_write("Write failed. Check the path or available memory.\n");
            }
        }
    } else if (command_matches(command, "ls")) {
        ramfs_list();
    } else if (command_starts_with(command, "cat ")) {
        if (!ramfs_cat(command + 4)) {
            console_write("File not found. Try: cat etc/motd.txt\n");
        }
    } else if (command[0] != '\0') {
        console_write("Unknown command. Type help.\n");
    }
}

static void shell_run(struct limine_framebuffer *fb) {
    console_init(fb);
    console_write("PRATEEKOS TEXT CONSOLE\n");
    console_write("=================\n");
    console_write("Keyboard shell ready. Type help.\n\n");

    if (module_request.response == NULL) {
        console_write("ERROR: Limine module response missing\n");
    } else if (module_request.response->module_count < 2) {
        console_write("ERROR: Initramfs module missing\n");
    } else {
        console_write("User ELF and initramfs modules loaded\n");
    }

    char command[80];
    size_t length = 0;
    return_to_desktop = 0;
    console_write("prateekos> ");

    while (!return_to_desktop) {
        mouse_discard_pending();
        uint8_t scancode;
        if (!keyboard_poll(&scancode)) {
            __asm__ volatile ("pause");
            continue;
        }
        if ((scancode & 0x80) != 0) continue;

        if (scancode == 0x1C) {
            console_putc('\n');
            command[length] = '\0';
            run_command(command);
            length = 0;
            if (!return_to_desktop) console_write("prateekos> ");
            continue;
        }
        if (scancode == 0x0E) {
            if (length > 0) {
                length--;
                console_putc('\b');
            }
            continue;
        }
        if (scancode < 128 && keymap[scancode] != '\0' &&
            length < sizeof(command) - 1) {
            command[length++] = keymap[scancode];
            console_putc(keymap[scancode]);
        }
    }
}

void kmain(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) hcf();
    if (memmap_request.response == NULL || hhdm_request.response == NULL) hcf();

    pmm_init(memmap_request.response, hhdm_request.response->offset);
    if (!vmm_init(hhdm_request.response->offset)) hcf();

    gdt_init();
    idt_init();
    __asm__ volatile ("int3");

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) hcf();

    for (uint64_t i = 0; i < framebuffer_request.response->framebuffer_count; i++) {
        struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[i];
        if (framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB || framebuffer->bpp != 32) hcf();
        fb_pattern(framebuffer);
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    if (module_request.response != NULL && module_request.response->module_count >= 2) {
        ramfs_init(module_request.response->modules[1]);
    }

    timer_init(100);
    __asm__ volatile ("sti");

    for (;;) {
        (void)gui_run(fb);
        shell_run(fb);
    }
}
