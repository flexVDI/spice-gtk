/*
 * Small jpeg decoder library (header file)
 *
 * Copyright (c) 2006, Luc Saillard <luc@saillard.org>
 * Copyright (c) 2012 Intel Corporation.
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * - Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 * - Neither the name of the author nor the names of its contributors may be
 *  used to endorse or promote products derived from this software without
 *  specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */


#ifndef __JPEGDEC_H__
#define __JPEGDEC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <va/va.h>

struct jdec_private;
typedef struct display_private display_private;

typedef struct tinyjpeg_session {
    display_private *dpy_priv;
    VADisplay va_dpy;
    VAStatus va_status;
    VAConfigAttrib attrib;
    VAConfigID config_id;
    struct jdec_private *jdec;
    VARectangle src_rect;
    VARectangle dst_rect;
} tinyjpeg_session;

typedef struct {
    VAStatus (*open_display)(tinyjpeg_session *session);
    void (*close_display)(tinyjpeg_session *session);
    VAStatus (*put_surface)(tinyjpeg_session *session, VASurfaceID surface);
} VADisplayHooks;

void set_va_display_hooks(VADisplayHooks *hooks);

/* Flags that can be set by any applications */
#define TINYJPEG_FLAGS_MJPEG_TABLE	(1<<1)

/* Format accepted in outout */
enum tinyjpeg_fmt {
   TINYJPEG_FMT_GREY = 1,
   TINYJPEG_FMT_BGR24,
   TINYJPEG_FMT_RGB24,
   TINYJPEG_FMT_YUV420P,
};

struct jdec_private *tinyjpeg_init(void);
void tinyjpeg_free(struct jdec_private *priv);

int tinyjpeg_parse_header(tinyjpeg_session *session, const unsigned char *buf, unsigned int size);
tinyjpeg_session * tinyjpeg_open_display(void);
void tinyjpeg_close_display(tinyjpeg_session *session);
int tinyjpeg_decode(tinyjpeg_session *session);
const char *tinyjpeg_get_errorstring(struct jdec_private *priv);
void tinyjpeg_get_size(struct jdec_private *priv, unsigned int *width, unsigned int *height);

#ifdef __cplusplus
}
#endif

#endif



