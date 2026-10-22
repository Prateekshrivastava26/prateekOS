#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define VMM_PAGE_SIZE 4096ULL
#define VMM_PRESENT   0x001ULL
#define VMM_WRITABLE  0x002ULL
#define VMM_USER      0x004ULL

int vmm_init(uint64_t hhdm_offset);
int vmm_map_page(uint64_t virtual_address, void *physical_page, uint64_t flags);
void *vmm_unmap_page(uint64_t virtual_address);
int vmm_self_test(void);

#endif
