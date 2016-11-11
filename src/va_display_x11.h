/*
 * Copyright Flexible Software Solutions S.L. 2014
 */

#ifndef __VA_DISPLAY_X11_H
#define __VA_DISPLAY_X11_H

#include <cairo/cairo.h>
#include "tinyjpeg.h"

void va_x11_set_display_hooks(void);
void va_x11_draw_frame(tinyjpeg_session *session, cairo_t *cr);

#endif /* __VA_DISPLAY_X11_H */
