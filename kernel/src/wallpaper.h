#ifndef PRATEEKOS_WALLPAPER_H
#define PRATEEKOS_WALLPAPER_H
#include <stdint.h>
typedef struct {const uint8_t *pixels;uint32_t width,height,stride;int top_down;} wallpaper_t;
int wallpaper_decode(wallpaper_t *image,const uint8_t *data,uint64_t size);
#endif
