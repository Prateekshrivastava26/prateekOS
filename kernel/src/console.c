#include "console.h"

#include <stdint.h>
#include "font8x8_basic.h"

static struct limine_framebuffer *active_fb;
static uint64_t cursor_x;
static uint64_t cursor_y;
static uint32_t foreground;
static uint32_t background;

static uint32_t color_channel(uint8_t value, uint8_t size, uint8_t shift) {
    if (size == 0) {
        return 0;
    }

    uint64_t maximum = (1ULL << size) - 1;
    uint32_t scaled = (uint32_t)(((uint64_t)value * maximum + 127) / 255);
    return scaled << shift;
}

static uint32_t make_color(uint8_t red, uint8_t green, uint8_t blue) {
    return color_channel(red, active_fb->red_mask_size,
                         active_fb->red_mask_shift)
         | color_channel(green, active_fb->green_mask_size,
                         active_fb->green_mask_shift)
         | color_channel(blue, active_fb->blue_mask_size,
                         active_fb->blue_mask_shift);
}

static void put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (x >= active_fb->width || y >= active_fb->height) {
        return;
    }

    volatile uint32_t *pixels = active_fb->address;
    uint64_t pixels_per_row = active_fb->pitch / sizeof(uint32_t);
    pixels[y * pixels_per_row + x] = color;
}

static void fill_cell(uint64_t x, uint64_t y, uint32_t color) {
    for (uint64_t row = 0; row < 8; row++) {
        for (uint64_t column = 0; column < 8; column++) {
            put_pixel(x + column, y + row, color);
        }
    }
}

static void scroll_if_needed(void) {
    if (active_fb == 0 || active_fb->height < 8 ||
        cursor_y + 8 <= active_fb->height) {
        return;
    }

    volatile uint32_t *pixels = active_fb->address;
    uint64_t pixels_per_row = active_fb->pitch / sizeof(uint32_t);

    for (uint64_t y = 0; y + 8 < active_fb->height; y++) {
        for (uint64_t x = 0; x < active_fb->width; x++) {
            pixels[y * pixels_per_row + x] =
                pixels[(y + 8) * pixels_per_row + x];
        }
    }

    uint64_t first_clear_row = active_fb->height - 8;
    for (uint64_t y = first_clear_row; y < active_fb->height; y++) {
        for (uint64_t x = 0; x < active_fb->width; x++) {
            pixels[y * pixels_per_row + x] = background;
        }
    }

    cursor_y = first_clear_row;
}

void console_clear(void) {
    if (active_fb == 0) {
        return;
    }

    volatile uint32_t *pixels = active_fb->address;
    uint64_t pixel_count =
        (active_fb->pitch / sizeof(uint32_t)) * active_fb->height;

    for (uint64_t i = 0; i < pixel_count; i++) {
        pixels[i] = background;
    }

    cursor_x = 0;
    cursor_y = 0;
}

void console_init(struct limine_framebuffer *framebuffer) {
    active_fb = framebuffer;

    foreground = make_color(235, 240, 255);
    background = make_color(12, 18, 32);

    console_clear();
}

void console_putc(char character) {
    if (active_fb == 0) {
        return;
    }

    if (character == '\r') {
        return;
    }

    if (character == '\n') {
        cursor_x = 0;
        cursor_y += 8;
        scroll_if_needed();
        return;
    }

    if (character == '\b') {
        if (cursor_x >= 8) {
            cursor_x -= 8;
            fill_cell(cursor_x, cursor_y, background);
        }
        return;
    }

    if (character == '\t') {
        console_putc(' ');
        console_putc(' ');
        console_putc(' ');
        console_putc(' ');
        return;
    }

    if (character < 0 || (uint8_t)character >= 128) {
        character = '?';
    }

    if (cursor_x + 8 > active_fb->width) {
        cursor_x = 0;
        cursor_y += 8;
    }

    scroll_if_needed();
    fill_cell(cursor_x, cursor_y, background);

    for (uint64_t row = 0; row < 8; row++) {
        uint8_t bits = (uint8_t)font8x8_basic[(uint8_t)character][row];

        for (uint64_t column = 0; column < 8; column++) {
            if ((bits & (1u << column)) != 0) {
                put_pixel(cursor_x + column, cursor_y + row, foreground);
            }
        }
    }

    cursor_x += 8;
}

void console_write(const char *text) {
    if (text == 0) {
        return;
    }

    while (*text != '\0') {
        console_putc(*text);
        text++;
    }
}

