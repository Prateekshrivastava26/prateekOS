#include "vmm.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define PAGE_SIZE_MASK    (VMM_PAGE_SIZE - 1)
#define LARGE_PAGE_FLAG   (1ULL << 7)
#define TEST_VIRTUAL_ADDRESS 0xFFFF900000000000ULL

static uint64_t hhdm_offset;
static uint64_t *pml4;

static int is_canonical(uint64_t address) {
    uint64_t upper = address >> 48;

    if (address & (1ULL << 47)) {
        return upper == 0xFFFF;
    }

    return upper == 0;
}

static void *physical_to_hhdm(uint64_t physical_address) {
    if (physical_address > UINT64_MAX - hhdm_offset) {
        return NULL;
    }

    return (void *)(uintptr_t)(physical_address + hhdm_offset);
}

static uint64_t table_to_physical(uint64_t *table) {
    return (uint64_t)(uintptr_t)table - hhdm_offset;
}

int vmm_init(uint64_t offset) {
    uint64_t cr4;
    uint64_t cr3;

    hhdm_offset = offset;

    /* This implementation supports four-level x86-64 paging. */
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & (1ULL << 12)) {
        return 0; /* LA57 means five-level paging is active. */
    }

    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));

    pml4 = physical_to_hhdm(cr3 & PAGE_ADDRESS_MASK);
    return pml4 != NULL;
}

int vmm_map_page(uint64_t virtual_address, void *physical_page, uint64_t flags) {
    if (pml4 == NULL || physical_page == NULL ||
        !is_canonical(virtual_address) ||
        (virtual_address & PAGE_SIZE_MASK) != 0 ||
        ((uint64_t)(uintptr_t)physical_page & PAGE_SIZE_MASK) != 0) {
        return 0;
    }

    uint64_t page_pointer = (uint64_t)(uintptr_t)physical_page;
    if (page_pointer < hhdm_offset) {
        return 0;
    }

    uint64_t physical_address = page_pointer - hhdm_offset;
    uint16_t indices[4] = {
        (uint16_t)((virtual_address >> 39) & 0x1FF),
        (uint16_t)((virtual_address >> 30) & 0x1FF),
        (uint16_t)((virtual_address >> 21) & 0x1FF),
        (uint16_t)((virtual_address >> 12) & 0x1FF)
    };

    uint64_t *table = pml4;

    for (size_t level = 0; level < 3; level++) {
        uint64_t *entry = &table[indices[level]];

        if ((*entry & VMM_PRESENT) == 0) {
            uint64_t *new_table = pmm_alloc_page();
            if (new_table == NULL) {
                return 0;
            }

            for (size_t i = 0; i < 512; i++) {
                new_table[i] = 0;
            }

            *entry = table_to_physical(new_table) |
                     VMM_PRESENT | VMM_WRITABLE;
        } else if (*entry & LARGE_PAGE_FLAG) {
            return 0;
        }

        if (flags & VMM_WRITABLE) {
            *entry |= VMM_WRITABLE;
        }
        if (flags & VMM_USER) {
            *entry |= VMM_USER;
        }

        table = physical_to_hhdm(*entry & PAGE_ADDRESS_MASK);
        if (table == NULL) {
            return 0;
        }
    }

    uint64_t *leaf = &table[indices[3]];
    if (*leaf & VMM_PRESENT) {
        return 0; /* Do not overwrite an existing mapping. */
    }

    *leaf = physical_address |
            (flags & 0xFFFULL) |
            VMM_PRESENT;

    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return 1;
}

void *vmm_unmap_page(uint64_t virtual_address) {
    if (pml4 == NULL || !is_canonical(virtual_address) ||
        (virtual_address & PAGE_SIZE_MASK) != 0) {
        return NULL;
    }

    uint16_t indices[4] = {
        (uint16_t)((virtual_address >> 39) & 0x1FF),
        (uint16_t)((virtual_address >> 30) & 0x1FF),
        (uint16_t)((virtual_address >> 21) & 0x1FF),
        (uint16_t)((virtual_address >> 12) & 0x1FF)
    };

    uint64_t *table = pml4;

    for (size_t level = 0; level < 3; level++) {
        uint64_t entry = table[indices[level]];

        if ((entry & VMM_PRESENT) == 0 || (entry & LARGE_PAGE_FLAG)) {
            return NULL;
        }

        table = physical_to_hhdm(entry & PAGE_ADDRESS_MASK);
        if (table == NULL) {
            return NULL;
        }
    }

    uint64_t *leaf = &table[indices[3]];
    if ((*leaf & VMM_PRESENT) == 0) {
        return NULL;
    }

    uint64_t physical_address = *leaf & PAGE_ADDRESS_MASK;
    *leaf = 0;

    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return physical_to_hhdm(physical_address);
}

int vmm_self_test(void) {
    const uint64_t expected = 0x5052415445454B4FULL;
    void *page = pmm_alloc_page();

    if (page == NULL) {
        return 0;
    }

    if (!vmm_map_page(TEST_VIRTUAL_ADDRESS, page, VMM_WRITABLE)) {
        pmm_free_page(page);
        return 0;
    }

    volatile uint64_t *test_address =
        (volatile uint64_t *)(uintptr_t)TEST_VIRTUAL_ADDRESS;
    *test_address = expected;
    int passed = (*test_address == expected);

    void *unmapped_page = vmm_unmap_page(TEST_VIRTUAL_ADDRESS);
    if (unmapped_page != page) {
        passed = 0;
    }

    pmm_free_page(page);
    return passed;
}
