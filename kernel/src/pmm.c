#include "pmm.h"

#include <stdint.h>

#ifndef LIMINE_MEMMAP_USABLE
#define LIMINE_MEMMAP_USABLE 0
#endif

typedef struct free_page {
    struct free_page *next;
} free_page_t;

static free_page_t *free_list;
static uint64_t free_pages;
static uint64_t total_pages;
static uint64_t direct_map_offset;

void pmm_init(struct limine_memmap_response *memory_map,
              uint64_t hhdm_offset) {
    free_list = 0;
    free_pages = 0;
    total_pages = 0;
    direct_map_offset = hhdm_offset;

    if (memory_map == 0) {
        return;
    }

    for (uint64_t i = 0; i < memory_map->entry_count; i++) {
        struct limine_memmap_entry *entry = memory_map->entries[i];

        if (entry == 0 || entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        if (entry->base > UINT64_MAX - (PMM_PAGE_SIZE - 1)) {
            continue;
        }

        uint64_t start =
            (entry->base + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);

        if (entry->length > UINT64_MAX - entry->base) {
            continue;
        }

        uint64_t end =
            (entry->base + entry->length) & ~(PMM_PAGE_SIZE - 1);

        for (uint64_t physical = start;
             physical < end;
             physical += PMM_PAGE_SIZE) {
            if (physical > UINT64_MAX - direct_map_offset) {
                break;
            }

            free_page_t *page =
                (free_page_t *)(uintptr_t)(physical + direct_map_offset);

            page->next = free_list;
            free_list = page;
            free_pages++;
            total_pages++;
        }
    }
}

void *pmm_alloc_page(void) {
    if (free_list == 0) {
        return 0;
    }

    free_page_t *page = free_list;
    free_list = page->next;
    free_pages--;

    return page;
}

void pmm_free_page(void *address) {
    if (address == 0) {
        return;
    }

    free_page_t *page = address;
    page->next = free_list;
    free_list = page;
    free_pages++;
}

uint64_t pmm_free_page_count(void) {
    return free_pages;
}

uint64_t pmm_total_page_count(void) {
    return total_pages;
}
