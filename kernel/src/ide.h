#ifndef IDE_H
#define IDE_H

#include <stdint.h>

int ide_read_sector(uint32_t lba, uint16_t *buffer);
int ide_write_sector(uint32_t lba, const uint16_t *buffer);
int ide_persistence_test(void);
uint64_t ide_capacity_bytes(void);

#endif
