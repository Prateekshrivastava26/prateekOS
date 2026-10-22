#include "heap.h"
#include "pmm.h"

#include <stdint.h>

#define HEAP_MAGIC 0x5052415445454B48ULL

typedef struct {
    uint64_t magic;
    size_t requested_size;
} heap_header_t;

void *kmalloc(size_t size) {
    if (size == 0 || size > PMM_PAGE_SIZE - sizeof(heap_header_t)) {
        return NULL;
    }

    heap_header_t *header = pmm_alloc_page();
    if (header == NULL) {
        return NULL;
    }

    header->magic = HEAP_MAGIC;
    header->requested_size = size;

    return (void *)(header + 1);
}

void kfree(void *address) {
    if (address == NULL) {
        return;
    }

    heap_header_t *header = ((heap_header_t *)address) - 1;

    if (header->magic != HEAP_MAGIC) {
        return;
    }

    header->magic = 0;
    pmm_free_page(header);
}
