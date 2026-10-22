#include "ide.h"

#include <stdint.h>

#define ATA_DATA       0x1F0
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW    0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HIGH   0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_CONTROL    0x3F6

#define ATA_BUSY  0x80
#define ATA_ERROR 0x01
#define ATA_FAULT 0x20
#define ATA_DRQ   0x08

#define TEST_LBA 2048

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void delay_400ns(void) {
    for (int i = 0; i < 4; i++) {
        (void)inb(ATA_CONTROL);
    }
}

static int wait_ready(int wait_for_data) {
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_STATUS);

        if (status == 0 || status == 0xFF) {
            return 0;
        }

        if ((status & ATA_BUSY) != 0) {
            continue;
        }

        if ((status & (ATA_ERROR | ATA_FAULT)) != 0) {
            return 0;
        }

        if (!wait_for_data || (status & ATA_DRQ) != 0) {
            return 1;
        }
    }

    return 0;
}

static int select_sector(uint32_t lba) {
    if (lba > 0x0FFFFFFF) {
        return 0;
    }

    /* Disable ATA device interrupts; this driver polls the status port. */
    outb(ATA_CONTROL, 0x02);
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    delay_400ns();

    if (!wait_ready(0)) {
        return 0;
    }

    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    return 1;
}

int ide_read_sector(uint32_t lba, uint16_t *buffer) {
    if (buffer == 0 || !select_sector(lba)) {
        return 0;
    }

    outb(ATA_COMMAND, 0x20);

    if (!wait_ready(1)) {
        return 0;
    }

    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_DATA);
    }

    return wait_ready(0);
}

int ide_write_sector(uint32_t lba, const uint16_t *buffer) {
    if (buffer == 0 || !select_sector(lba)) {
        return 0;
    }

    outb(ATA_COMMAND, 0x30);

    if (!wait_ready(1)) {
        return 0;
    }

    for (int i = 0; i < 256; i++) {
        outw(ATA_DATA, buffer[i]);
    }

    if (!wait_ready(0)) {
        return 0;
    }

    outb(ATA_COMMAND, 0xE7); /* Flush the disk's write cache. */
    return wait_ready(0);
}

int ide_persistence_test(void) {
    static const uint16_t marker[4] = {
        0x5052, 0x4154, 0x4545, 0x4B4F
    };
    uint16_t sector[256];

    if (!ide_read_sector(TEST_LBA, sector)) {
        return -1;
    }

    if (sector[0] == marker[0] &&
        sector[1] == marker[1] &&
        sector[2] == marker[2] &&
        sector[3] == marker[3]) {
        return 1;
    }

    for (int i = 0; i < 256; i++) {
        sector[i] = 0;
    }
    for (int i = 0; i < 4; i++) {
        sector[i] = marker[i];
    }

    if (!ide_write_sector(TEST_LBA, sector) || !ide_read_sector(TEST_LBA, sector)) {
        return -1;
    }

    for (int i = 0; i < 4; i++) {
        if (sector[i] != marker[i]) {
            return -1;
        }
    }

    return 0;
}

/* IDENTIFY DEVICE reports the capacity without writing to the disk. */
uint64_t ide_capacity_bytes(void) {
    static uint64_t capacity;
    if(capacity) return capacity;
    outb(ATA_CONTROL,0x02);
    outb(ATA_DRIVE,0xE0);
    delay_400ns();
    uint32_t attempts=0;
    while(inb(ATA_STATUS) & ATA_BUSY) {
        if(++attempts==1000000) return 0;
    }
    outb(ATA_SECTOR_COUNT,0);
    outb(ATA_LBA_LOW,0); outb(ATA_LBA_MID,0); outb(ATA_LBA_HIGH,0);
    outb(ATA_COMMAND,0xEC);
    delay_400ns();
    if(!wait_ready(1)) return 0;
    if(inb(ATA_LBA_MID)!=0 || inb(ATA_LBA_HIGH)!=0) return 0;
    uint16_t words[256];
    for(unsigned i=0;i<256;i++) words[i]=inw(ATA_DATA);
    if(!wait_ready(0) || !(words[49] & (1U<<9))) return 0;
    uint64_t sectors=(uint64_t)words[60] | ((uint64_t)words[61]<<16);
    /* This driver addresses up to 2^28 sectors (128 GiB). */
    if(sectors>0x10000000ULL) sectors=0x10000000ULL;
    capacity=sectors*512ULL;
    return capacity;
}
