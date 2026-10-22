#include "ramfs.h"
#include "console.h"
#include "heap.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

static const uint8_t *archive;
static uint64_t archive_size;

typedef struct overlay_file {
    struct overlay_file *next;
    uint64_t size;
    char path[128];
    uint8_t data[];
} overlay_file_t;

static overlay_file_t *overlay_files;

static uint64_t octal_value(const uint8_t *field, size_t length) {
    uint64_t value = 0;

    for (size_t i = 0; i < length; i++) {
        if (field[i] >= '0' && field[i] <= '7') {
            value = value * 8 + (field[i] - '0');
        }
    }

    return value;
}

static size_t field_length(const uint8_t *field, size_t maximum) {
    size_t length = 0;

    while (length < maximum && field[length] != 0) {
        length++;
    }

    return length;
}

static size_t string_length(const char *string, size_t maximum) {
    size_t length = 0;

    while (length < maximum && string[length] != '\0') {
        length++;
    }

    return length;
}

static void entry_path(const uint8_t *header, char *out) {
    size_t prefix_length = field_length(header + 345, 155);
    size_t name_length = field_length(header, 100);
    size_t used = 0;

    for (size_t i = 0; i < prefix_length && used < 255; i++) {
        out[used++] = (char)header[345 + i];
    }

    if (prefix_length != 0 && name_length != 0 && used < 255) {
        out[used++] = '/';
    }

    for (size_t i = 0; i < name_length && used < 255; i++) {
        out[used++] = (char)header[i];
    }

    size_t start = (used >= 2 && out[0] == '.' && out[1] == '/') ? 2 : 0;
    if (start != 0) {
        for (size_t i = start; i < used; i++) {
            out[i - start] = out[i];
        }
        used -= start;
    }

    out[used] = '\0';
}

static int next_entry(uint64_t *offset,
                      const uint8_t **header_out,
                      const uint8_t **data_out,
                      uint64_t *size_out) {
    if (archive == NULL || *offset > archive_size ||
        archive_size - *offset < 512) {
        return 0;
    }

    const uint8_t *header = archive + *offset;
    if (header[0] == 0) {
        return 0;
    }

    uint64_t file_size = octal_value(header + 124, 12);
    uint64_t data_offset = *offset + 512;

    if (file_size > archive_size - data_offset ||
        file_size > UINT64_MAX - 511) {
        return 0;
    }

    uint64_t padded_size = (file_size + 511) & ~511ULL;
    if (padded_size > archive_size - data_offset) {
        return 0;
    }

    *header_out = header;
    *data_out = archive + data_offset;
    *size_out = file_size;
    *offset = data_offset + padded_size;
    return 1;
}

static int is_regular_file(const uint8_t *header) {
    return header[156] == 0 || header[156] == '0';
}

static int paths_equal(const char *left, const char *right) {
    while (*left == '/') {
        left++;
    }
    while (*right == '/') {
        right++;
    }

    while (*left != '\0' && *right != '\0' && *left == *right) {
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static overlay_file_t *find_overlay_file(const char *path) {
    for (overlay_file_t *file = overlay_files;
         file != NULL;
         file = file->next) {
        if (paths_equal(path, file->path)) {
            return file;
        }
    }

    return NULL;
}

void ramfs_init(struct limine_file *module) {
    archive = NULL;
    archive_size = 0;
    overlay_files = NULL;

    if (module != NULL && module->address != NULL) {
        archive = module->address;
        archive_size = module->size;
    }
}

int ramfs_write_file(const char *path, const char *text) {
    if (path == NULL || text == NULL || *path == '\0') {
        return 0;
    }

    size_t path_size = string_length(path, sizeof(((overlay_file_t *)0)->path));
    if (path_size == 0 || path_size >= sizeof(((overlay_file_t *)0)->path)) {
        return 0;
    }

    size_t text_size = string_length(text, PMM_PAGE_SIZE);
    if (text_size == PMM_PAGE_SIZE) {
        return 0;
    }

    size_t allocation_size = sizeof(overlay_file_t) + text_size;
    if (allocation_size > PMM_PAGE_SIZE - 16) {
        return 0;
    }

    overlay_file_t *new_file = kmalloc(allocation_size);
    if (new_file == NULL) {
        return 0;
    }

    new_file->size = text_size;
    for (size_t i = 0; i < path_size; i++) {
        new_file->path[i] = path[i];
    }
    new_file->path[path_size] = '\0';

    for (size_t i = 0; i < text_size; i++) {
        new_file->data[i] = (uint8_t)text[i];
    }

    overlay_file_t **link = &overlay_files;
    while (*link != NULL && !paths_equal(path, (*link)->path)) {
        link = &(*link)->next;
    }

    if (*link != NULL) {
        overlay_file_t *old_file = *link;
        new_file->next = old_file->next;
        *link = new_file;
        kfree(old_file);
    } else {
        new_file->next = overlay_files;
        overlay_files = new_file;
    }

    return 1;
}

void ramfs_list(void) {
    if (archive == NULL && overlay_files == NULL) {
        console_write("RAM filesystem unavailable\n");
        return;
    }

    int found = 0;
    uint64_t offset = 0;
    const uint8_t *header;
    const uint8_t *data;
    uint64_t size;

    while (next_entry(&offset, &header, &data, &size)) {
        (void)data;
        (void)size;

        if (!is_regular_file(header)) {
            continue;
        }

        char path[256];
        entry_path(header, path);

        if (find_overlay_file(path) == NULL) {
            console_write(path);
            console_putc('\n');
            found = 1;
        }
    }

    for (overlay_file_t *file = overlay_files;
         file != NULL;
         file = file->next) {
        console_write(file->path);
        console_putc('\n');
        found = 1;
    }

    if (!found) {
        console_write("No files found\n");
    }
}


int ramfs_file_path_at(uint64_t wanted, char *out, size_t capacity) {
    if (out == NULL || capacity == 0) return 0;
    uint64_t index = 0;
    uint64_t offset = 0;
    const uint8_t *header;
    const uint8_t *data;
    uint64_t size;

    while (next_entry(&offset, &header, &data, &size)) {
        (void)data;
        (void)size;
        if (!is_regular_file(header)) continue;
        char path[256];
        entry_path(header, path);
        if (find_overlay_file(path) != NULL) continue;
        if (index++ != wanted) continue;
        size_t length = string_length(path, sizeof(path));
        if (length + 1 > capacity) return 0;
        for (size_t i = 0; i <= length; i++) out[i] = path[i];
        return 1;
    }

    for (overlay_file_t *file = overlay_files; file != NULL; file = file->next) {
        if (index++ != wanted) continue;
        size_t length = string_length(file->path, sizeof(file->path));
        if (length + 1 > capacity) return 0;
        for (size_t i = 0; i <= length; i++) out[i] = file->path[i];
        return 1;
    }
    return 0;
}

int ramfs_get_file(const char *path, const uint8_t **file_data,
                   uint64_t *file_size) {
    if (path == NULL || file_data == NULL || file_size == NULL) {
        return 0;
    }

    overlay_file_t *overlay = find_overlay_file(path);
    if (overlay != NULL) {
        *file_data = overlay->data;
        *file_size = overlay->size;
        return 1;
    }

    uint64_t offset = 0;
    const uint8_t *header;
    const uint8_t *data;
    uint64_t size;

    while (next_entry(&offset, &header, &data, &size)) {
        if (!is_regular_file(header)) {
            continue;
        }

        char stored_path[256];
        entry_path(header, stored_path);

        if (paths_equal(path, stored_path)) {
            *file_data = data;
            *file_size = size;
            return 1;
        }
    }

    return 0;
}

int ramfs_remove_file(const char *path) {
    if (path == NULL || *path == '\0') {
        return 0;
    }

    overlay_file_t **link = &overlay_files;

    while (*link != NULL) {
        if (paths_equal(path, (*link)->path)) {
            overlay_file_t *file = *link;
            *link = file->next;
            kfree(file);
            return 1;
        }

        link = &(*link)->next;
    }

    return 0;
}

int ramfs_cat(const char *path) {
    const uint8_t *data;
    uint64_t size;

    if (path == NULL || !ramfs_get_file(path, &data, &size)) {
        return 0;
    }

    for (uint64_t i = 0; i < size; i++) {
        console_putc((char)data[i]);
    }

    return 1;
}
