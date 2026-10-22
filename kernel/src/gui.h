#ifndef GUI_H
#define GUI_H

#include <limine.h>

/* Draw the desktop; return 1 when the user chooses Terminal. */
int gui_run(struct limine_framebuffer *framebuffer);

#endif
