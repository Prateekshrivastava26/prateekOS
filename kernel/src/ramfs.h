#ifndef RAMFS_H
#define RAMFS_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>

void ramfs_init(struct limine_file *module);
void ramfs_list(void);
int ramfs_file_path_at(uint64_t index, char *out, size_t capacity);
int ramfs_get_file(const char *path, const uint8_t **data, uint64_t *size);
int ramfs_cat(const char *path);
int ramfs_write_file(const char *path, const char *text);
int ramfs_remove_file(const char *path);

#endif
