#include "user.h"
#include "pmm.h"
#include "vmm.h"

#include <stddef.h>
#include <stdint.h>

#define USER_CODE_ADDRESS  0x0000400000000000ULL
#define USER_DATA_ADDRESS  0x0000400000001000ULL
#define USER_STACK_ADDRESS 0x0000400000002000ULL
#define USER_STACK_TOP     0x0000400000003000ULL

#define ELF_PT_LOAD 1
#define ELF_PF_X    1
#define ELF_PF_W    2

typedef struct __attribute__((packed)) {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf64_header_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} elf64_phdr_t;

extern void enter_user_mode(uint64_t user_rip, uint64_t user_rsp);

static void zero_page(uint8_t *page) {
    for (uint64_t i = 0; i < PMM_PAGE_SIZE; i++) {
        page[i] = 0;
    }
}

int user_test_run(const void *program_image, uint64_t image_size) {
    if (program_image == NULL ||
        image_size < sizeof(elf64_header_t)) {
        return 0;
    }

    const uint8_t *image = program_image;
    const elf64_header_t *eh = (const elf64_header_t *)image;

    if (eh->ident[0] != 0x7F || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F' ||
        eh->ident[4] != 2 || eh->ident[5] != 1 ||
        eh->type != 2 || eh->machine != 62 || eh->version != 1 ||
        eh->phentsize != sizeof(elf64_phdr_t) ||
        eh->phnum == 0 || eh->phnum > 8 ||
        eh->phoff > image_size ||
        eh->phnum > (image_size - eh->phoff) / eh->phentsize) {
        return 0;
    }

    const elf64_phdr_t *code = NULL;
    const elf64_phdr_t *data = NULL;

    for (uint16_t i = 0; i < eh->phnum; i++) {
        const elf64_phdr_t *ph =
            (const elf64_phdr_t *)(image + eh->phoff +
                                   (uint64_t)i * eh->phentsize);

        if (ph->type != ELF_PT_LOAD) {
            continue;
        }

        if (ph->memsz == 0 || ph->memsz > PMM_PAGE_SIZE ||
            ph->filesz > ph->memsz || ph->offset > image_size ||
            ph->filesz > image_size - ph->offset) {
            return 0;
        }

        if (ph->vaddr == USER_CODE_ADDRESS &&
            (ph->flags & ELF_PF_X) && !(ph->flags & ELF_PF_W) &&
            code == NULL) {
            code = ph;
        } else if (ph->vaddr == USER_DATA_ADDRESS &&
                   (ph->flags & ELF_PF_W) && !(ph->flags & ELF_PF_X) &&
                   data == NULL) {
            data = ph;
        } else {
            return 0;
        }
    }

    if (code == NULL || data == NULL ||
        eh->entry < code->vaddr ||
        eh->entry >= code->vaddr + code->memsz) {
        return 0;
    }

    void *code_page = pmm_alloc_page();
    void *data_page = pmm_alloc_page();
    void *stack_page = pmm_alloc_page();

    if (code_page == NULL || data_page == NULL || stack_page == NULL) {
        if (code_page != NULL) pmm_free_page(code_page);
        if (data_page != NULL) pmm_free_page(data_page);
        if (stack_page != NULL) pmm_free_page(stack_page);
        return 0;
    }

    zero_page(code_page);
    zero_page(data_page);
    zero_page(stack_page);

    const uint8_t *code_source = image + code->offset;
    const uint8_t *data_source = image + data->offset;
    uint8_t *code_destination = code_page;
    uint8_t *data_destination = data_page;

    for (uint64_t i = 0; i < code->filesz; i++) {
        code_destination[i] = code_source[i];
    }
    for (uint64_t i = 0; i < data->filesz; i++) {
        data_destination[i] = data_source[i];
    }

    if (!vmm_map_page(USER_CODE_ADDRESS, code_page, VMM_USER)) {
        pmm_free_page(code_page);
        pmm_free_page(data_page);
        pmm_free_page(stack_page);
        return 0;
    }

    if (!vmm_map_page(USER_DATA_ADDRESS, data_page,
                      VMM_USER | VMM_WRITABLE)) {
        vmm_unmap_page(USER_CODE_ADDRESS);
        pmm_free_page(code_page);
        pmm_free_page(data_page);
        pmm_free_page(stack_page);
        return 0;
    }

    if (!vmm_map_page(USER_STACK_ADDRESS, stack_page,
                      VMM_USER | VMM_WRITABLE)) {
        vmm_unmap_page(USER_CODE_ADDRESS);
        vmm_unmap_page(USER_DATA_ADDRESS);
        pmm_free_page(code_page);
        pmm_free_page(data_page);
        pmm_free_page(stack_page);
        return 0;
    }

    enter_user_mode(eh->entry, USER_STACK_TOP);

    void *old_code = vmm_unmap_page(USER_CODE_ADDRESS);
    void *old_data = vmm_unmap_page(USER_DATA_ADDRESS);
    void *old_stack = vmm_unmap_page(USER_STACK_ADDRESS);

    if (old_code != NULL) pmm_free_page(old_code);
    if (old_data != NULL) pmm_free_page(old_data);
    if (old_stack != NULL) pmm_free_page(old_stack);

    return old_code == code_page &&
           old_data == data_page &&
           old_stack == stack_page;
}
