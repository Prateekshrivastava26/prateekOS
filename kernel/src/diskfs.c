#include "diskfs.h"
#include "ide.h"
#include "console.h"

#include <stddef.h>
#include <stdint.h>

#define DISKFS_DIRECTORY_LBA 4096
#define DISKFS_FIRST_DATA_LBA 4097
#define DISKFS_FILE_COUNT 15
#define DISKFS_NAME_SIZE 28
#define DISKFS_FILE_SIZE 512

typedef struct __attribute__((packed)) {
    char name[DISKFS_NAME_SIZE];
    uint16_t size;
    uint8_t used;
    uint8_t reserved;
} diskfs_entry_t;

typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t version;
    uint32_t reserved;
    diskfs_entry_t entries[DISKFS_FILE_COUNT];
    uint8_t padding[16];
} diskfs_directory_t;

_Static_assert(sizeof(diskfs_directory_t) == 512,
               "Disk filesystem directory must be one sector");

static const char diskfs_magic[8] = {
    'P', 'R', 'A', 'T', 'E', 'E', 'K', '1'
};

static size_t bounded_length(const char *text, size_t maximum) {
    size_t length = 0;

    while (length < maximum && text[length] != '\0') {
        length++;
    }

    return length;
}

static int load_directory(diskfs_directory_t *directory) {
    if (!ide_read_sector(DISKFS_DIRECTORY_LBA, (uint16_t *)directory)) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(diskfs_magic); i++) {
        if (directory->magic[i] != diskfs_magic[i]) {
            return 0;
        }
    }

    return directory->version == 1;
}

static int name_matches(const char *path, const char stored[DISKFS_NAME_SIZE]) {
    size_t i = 0;

    while (i < DISKFS_NAME_SIZE &&
           path[i] != '\0' &&
           path[i] == stored[i]) {
        i++;
    }

    return i < DISKFS_NAME_SIZE &&
           path[i] == '\0' &&
           stored[i] == '\0';
}

int diskfs_format(void) {
    uint16_t sector[256] = {0};
    diskfs_directory_t *directory = (diskfs_directory_t *)sector;

    for (size_t i = 0; i < sizeof(diskfs_magic); i++) {
        directory->magic[i] = diskfs_magic[i];
    }
    directory->version = 1;

    return ide_write_sector(DISKFS_DIRECTORY_LBA, sector);
}

void diskfs_list(void) {
    uint16_t sector[256];
    diskfs_directory_t *directory = (diskfs_directory_t *)sector;

    if (!load_directory(directory)) {
        console_write("Disk filesystem is not formatted. Type diskformat.\n");
        return;
    }

    int found = 0;

    for (size_t i = 0; i < DISKFS_FILE_COUNT; i++) {
        if (directory->entries[i].used) {
            console_write(directory->entries[i].name);
            console_putc('\n');
            found = 1;
        }
    }

    if (!found) {
        console_write("No disk files found\n");
    }
}


uint32_t diskfs_get_names(char names[15][28]) {
    if (names == NULL) return 0;
    uint16_t sector[256];
    diskfs_directory_t *directory = (diskfs_directory_t *)sector;
    if (!load_directory(directory)) return 0;

    uint32_t count = 0;
    for (size_t i = 0; i < DISKFS_FILE_COUNT; i++) {
        if (!directory->entries[i].used) continue;
        for (size_t j = 0; j < DISKFS_NAME_SIZE; j++) {
            names[count][j] = directory->entries[i].name[j];
        }
        count++;
    }
    return count;
}

int diskfs_read_file(const char *path, uint8_t *out, size_t capacity,
                     size_t *size) {
    if (path == NULL || out == NULL || size == NULL) return 0;
    uint16_t directory_sector[256];
    diskfs_directory_t *directory = (diskfs_directory_t *)directory_sector;
    if (!load_directory(directory)) return 0;

    for (int i = 0; i < DISKFS_FILE_COUNT; i++) {
        diskfs_entry_t *entry = &directory->entries[i];
        if (!entry->used || !name_matches(path, entry->name) ||
            entry->size > capacity) continue;
        uint16_t data_sector[256];
        if (!ide_read_sector(DISKFS_FIRST_DATA_LBA + (uint32_t)i, data_sector)) return 0;
        const uint8_t *bytes = (const uint8_t *)data_sector;
        for (uint16_t j = 0; j < entry->size; j++) out[j] = bytes[j];
        *size = entry->size;
        return 1;
    }
    return 0;
}

int diskfs_write_file(const char *path, const char *text) {
    if (path == NULL || text == NULL) {
        return 0;
    }

    size_t name_length = bounded_length(path, DISKFS_NAME_SIZE);
    size_t text_length = bounded_length(text, DISKFS_FILE_SIZE + 1);

    if (name_length == 0 || name_length >= DISKFS_NAME_SIZE ||
        text_length > DISKFS_FILE_SIZE) {
        return 0;
    }

    uint16_t directory_sector[256];
    diskfs_directory_t *directory =
        (diskfs_directory_t *)directory_sector;

    if (!load_directory(directory)) {
        return 0;
    }

    int target = -1;
    int free_entry = -1;

    for (int i = 0; i < DISKFS_FILE_COUNT; i++) {
        if (directory->entries[i].used &&
            name_matches(path, directory->entries[i].name)) {
            target = i;
            break;
        }

        if (!directory->entries[i].used && free_entry < 0) {
            free_entry = i;
        }
    }

    if (target < 0) {
        target = free_entry;
    }
    if (target < 0) {
        return 0;
    }

    uint16_t data_sector[256] = {0};
    uint8_t *bytes = (uint8_t *)data_sector;

    for (size_t i = 0; i < text_length; i++) {
        bytes[i] = (uint8_t)text[i];
    }

    if (!ide_write_sector(DISKFS_FIRST_DATA_LBA + (uint32_t)target,
                          data_sector)) {
        return 0;
    }

    diskfs_entry_t *entry = &directory->entries[target];
    for (size_t i = 0; i < DISKFS_NAME_SIZE; i++) {
        entry->name[i] = '\0';
    }
    for (size_t i = 0; i < name_length; i++) {
        entry->name[i] = path[i];
    }

    entry->size = (uint16_t)text_length;
    entry->used = 1;

    return ide_write_sector(DISKFS_DIRECTORY_LBA, directory_sector);
}

int diskfs_cat(const char *path) {
    if (path == NULL) {
        return 0;
    }

    uint16_t directory_sector[256];
    diskfs_directory_t *directory =
        (diskfs_directory_t *)directory_sector;

    if (!load_directory(directory)) {
        return 0;
    }

    for (int i = 0; i < DISKFS_FILE_COUNT; i++) {
        diskfs_entry_t *entry = &directory->entries[i];

        if (entry->used && name_matches(path, entry->name)) {
            uint16_t data_sector[256];

            if (!ide_read_sector(DISKFS_FIRST_DATA_LBA + (uint32_t)i,
                                 data_sector)) {
                return 0;
            }

            uint8_t *bytes = (uint8_t *)data_sector;
            for (uint16_t j = 0; j < entry->size; j++) {
                console_putc((char)bytes[j]);
            }

            return 1;
        }
    }

    return 0;
}

int diskfs_remove_file(const char *path) {
    if (path == NULL) {
        return 0;
    }

    uint16_t directory_sector[256];
    diskfs_directory_t *directory =
        (diskfs_directory_t *)directory_sector;

    if (!load_directory(directory)) {
        return 0;
    }

    for (int i = 0; i < DISKFS_FILE_COUNT; i++) {
        diskfs_entry_t *entry = &directory->entries[i];

        if (entry->used && name_matches(path, entry->name)) {
            entry->used = 0;
            entry->size = 0;
            entry->name[0] = '\0';

            return ide_write_sector(DISKFS_DIRECTORY_LBA,
                                    directory_sector);
        }
    }

    return 0;
}
