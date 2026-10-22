#ifndef DISKFS_H
#define DISKFS_H

#include <stddef.h>
#include <stdint.h>

int diskfs_format(void);
void diskfs_list(void);
uint32_t diskfs_get_names(char names[15][28]);
int diskfs_write_file(const char *path, const char *text);
int diskfs_cat(const char *path);
int diskfs_read_file(const char *path, uint8_t *out, size_t capacity, size_t *size);
int diskfs_remove_file(const char *path);

#endif
