#ifndef PMM_H
#define PMM_H

#include <limine.h>
#include <stdint.h>

#define PMM_PAGE_SIZE 4096ULL

void pmm_init(struct limine_memmap_response *memory_map,
              uint64_t hhdm_offset);
void *pmm_alloc_page(void);
void pmm_free_page(void *page);
uint64_t pmm_free_page_count(void);
uint64_t pmm_total_page_count(void);

#endif
