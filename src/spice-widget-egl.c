/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2014-2016 Red Hat, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#include "config.h"

#include <math.h>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include "spice-widget.h"
#include "spice-widget-priv.h"
#include "spice-gtk-session-priv.h"
#include <libdrm/drm_fourcc.h>

#include <gdk/gdkx.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#define VERTS_ARRAY_SIZE (sizeof(GLfloat) * 4 * 4)
#define TEX_ARRAY_SIZE (sizeof(GLfloat) * 4 * 2)

static const char *spice_egl_vertex_src =       \
"                                               \
  #version 130\n                                \
                                                \
  in vec4 position;                             \
  in vec2 texcoords;                            \
  out vec2 tcoords;                             \
  uniform mat4 mproj;                           \
                                                \
  void main()                                   \
  {                                             \
    tcoords = texcoords;                        \
    gl_Position = mproj * position;             \
  }                                             \
";

static const char *spice_egl_fragment_src =     \
"                                               \
  #version 130\n                                \
                                                \
  in vec2 tcoords;                              \
  out vec4 fragmentColor;                       \
  uniform sampler2D samp;                       \
                                                \
  void  main()                                  \
  {                                             \
    fragmentColor = texture2D(samp, tcoords);   \
  }                                             \
";

static void apply_ortho(guint mproj, float left, float right,
                        float bottom, float top, float near, float far)

{
    float a = 2.0f / (right - left);
    float b = 2.0f / (top - bottom);
    float c = -2.0f / (far - near);

    float tx = - (right + left) / (right - left);
    float ty = - (top + bottom) / (top - bottom);
    float tz = - (far + near) / (far - near);

    float ortho[16] = {
        a, 0, 0, 0,
        0, b, 0, 0,
        0, 0, c, 0,
        tx, ty, tz, 1
    };

    glUniformMatrix4fv(mproj, 1, GL_FALSE, &ortho[0]);
}

static gboolean spice_egl_init_shaders(SpiceDisplay *display, GError **err)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    GLuint fs = 0, vs = 0, buf;
    GLint status, tex_loc, prog;
    gboolean success = FALSE;
    gchar log[1000] = { 0, };
    GLsizei len;

    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

    fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, (const char **)&spice_egl_fragment_src, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
    if (!status) {
        glGetShaderInfoLog(fs, sizeof(log), &len, log);
        g_set_error(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                    "failed to compile fragment shader: %s", log);
        goto end;
    }

    vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, (const char **)&spice_egl_vertex_src, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
    if (!status) {
        glGetShaderInfoLog(vs, sizeof(log), &len, log);
        g_set_error(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                    "failed to compile vertex shader: %s", log);
        goto end;
    }

    d->egl.prog = glCreateProgram();
    glAttachShader(d->egl.prog, fs);
    glAttachShader(d->egl.prog, vs);
    glLinkProgram(d->egl.prog);
    glGetProgramiv(d->egl.prog, GL_LINK_STATUS, &status);
    if (!status) {
        glGetProgramInfoLog(d->egl.prog, 1000, &len, log);
        g_set_error(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                    "error linking shaders: %s", log);
        goto end;
    }

    glUseProgram(d->egl.prog);
    glDetachShader(d->egl.prog, fs);
    glDetachShader(d->egl.prog, vs);

    d->egl.attr_pos = glGetAttribLocation(d->egl.prog, "position");
    g_assert(d->egl.attr_pos != -1);
    d->egl.attr_tex = glGetAttribLocation(d->egl.prog, "texcoords");
    g_assert(d->egl.attr_tex != -1);
    tex_loc = glGetUniformLocation(d->egl.prog, "samp");
    g_assert(tex_loc != -1);
    d->egl.mproj = glGetUniformLocation(d->egl.prog, "mproj");
    g_assert(d->egl.mproj != -1);

    glUniform1i(tex_loc, 0);

    /* we only use one VAO, so we always keep it bound */
    glGenVertexArrays(1, &buf);
    glBindVertexArray(buf);

    glGenBuffers(1, &buf);
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER,
                 VERTS_ARRAY_SIZE + TEX_ARRAY_SIZE,
                 NULL,
                 GL_STATIC_DRAW);
    d->egl.vbuf_id = buf;

    glGenTextures(1, &d->egl.tex_id);
    glGenTextures(1, &d->egl.tex_pointer_id);

    success = TRUE;

end:
    if (fs) {
        glDeleteShader(fs);
    }
    if (vs) {
        glDeleteShader(vs);
    }

    glUseProgram(prog);
    return success;
}

G_GNUC_INTERNAL
gboolean spice_egl_init(SpiceDisplay *display, GError **err)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    static const EGLint conf_att[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE,
    };
    static const EGLint ctx_att[] = {
#ifdef EGL_CONTEXT_MAJOR_VERSION
        EGL_CONTEXT_MAJOR_VERSION, 3,
#else
        EGL_CONTEXT_CLIENT_VERSION, 3,
#endif
        EGL_NONE
    };
    EGLBoolean b;
    EGLint major, minor, n;
    EGLNativeDisplayType dpy = 0;
    GdkDisplay *gdk_dpy = gdk_display_get_default();

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(gdk_dpy)) {
        d->egl.ctx = eglGetCurrentContext();
        dpy = (EGLNativeDisplayType)gdk_wayland_display_get_wl_display(gdk_dpy);
        d->egl.display = eglGetDisplay(dpy);
        goto end;
    }
#endif
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(gdk_dpy)) {
        dpy = (EGLNativeDisplayType)gdk_x11_display_get_xdisplay(gdk_dpy);
    }
#endif

    d->egl.display = eglGetDisplay(dpy);
    if (d->egl.display == EGL_NO_DISPLAY) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "failed to get EGL display");
        return FALSE;
    }

    if (!eglInitialize(d->egl.display, &major, &minor)) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "failed to init EGL display");
        return FALSE;
    }

    SPICE_DEBUG("EGL major/minor: %d.%d\n", major, minor);
    SPICE_DEBUG("EGL version: %s\n",
                eglQueryString(d->egl.display, EGL_VERSION));
    SPICE_DEBUG("EGL vendor: %s\n",
                eglQueryString(d->egl.display, EGL_VENDOR));
    SPICE_DEBUG("EGL extensions: %s\n",
                eglQueryString(d->egl.display, EGL_EXTENSIONS));

    b = eglBindAPI(EGL_OPENGL_API);
    if (!b) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "cannot bind OpenGL API");
        return FALSE;
    }

    b = eglChooseConfig(d->egl.display, conf_att, &d->egl.conf,
                        1, &n);

    if (!b || n != 1) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "cannot find suitable EGL config");
        return FALSE;
    }

    d->egl.ctx = eglCreateContext(d->egl.display,
                                  d->egl.conf,
                                  EGL_NO_CONTEXT,
                                  ctx_att);
    if (!d->egl.ctx) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "cannot create EGL context");
        return FALSE;
    }

    eglMakeCurrent(d->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   d->egl.ctx);

#ifdef GDK_WINDOWING_WAYLAND
end:
#endif

    if (!spice_egl_init_shaders(display, err))
        return FALSE;

    d->egl.context_ready = TRUE;

    if (spice_display_get_gl_scanout(SPICE_DISPLAY_CHANNEL(d->display)) != NULL) {
        SPICE_DEBUG("scanout present during egl init, updating widget");
        spice_display_widget_gl_scanout(display);
        spice_display_widget_update_monitor_area(display);
    }

    return TRUE;
}

static gboolean spice_widget_init_egl_win(SpiceDisplay *display, GdkWindow *win,
                                          GError **err)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    EGLNativeWindowType native = 0;
    EGLBoolean b;

    if (d->egl.surface)
        return TRUE;

#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_WINDOW(win)) {
        native = (EGLNativeWindowType)GDK_WINDOW_XID(win);
    }
#endif

    if (!native) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "this platform isn't supported");
        return FALSE;
    }

    d->egl.surface = eglCreateWindowSurface(d->egl.display,
                                            d->egl.conf,
                                            native, NULL);

    if (!d->egl.surface) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "failed to init egl surface");
        return FALSE;
    }

    b = eglMakeCurrent(d->egl.display,
                       d->egl.surface,
                       d->egl.surface,
                       d->egl.ctx);
    if (!b) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "failed to activate context");
        return FALSE;
    }

    return TRUE;
}

G_GNUC_INTERNAL
gboolean spice_egl_realize_display(SpiceDisplay *display, GdkWindow *win, GError **err)
{
    SPICE_DEBUG("egl realize");
    if (!spice_widget_init_egl_win(display, win, err))
        return FALSE;

    spice_egl_resize_display(display, gdk_window_get_width(win),
                             gdk_window_get_height(win));

    return TRUE;
}

G_GNUC_INTERNAL
void spice_egl_unrealize_display(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    SPICE_DEBUG("egl unrealize %p", d->egl.surface);

    if (d->egl.image != NULL) {
        eglDestroyImageKHR(d->egl.display, d->egl.image);
        d->egl.image = NULL;
    }

    if (d->egl.tex_id) {
        glDeleteTextures(1, &d->egl.tex_id);
        d->egl.tex_id = 0;
    }

    if (d->egl.tex_pointer_id) {
        glDeleteTextures(1, &d->egl.tex_pointer_id);
        d->egl.tex_pointer_id = 0;
    }

    if (d->egl.vbuf_id) {
        glDeleteBuffers(1, &d->egl.vbuf_id);
        d->egl.vbuf_id = 0;
    }

    if (d->egl.prog) {
        glDeleteProgram(d->egl.prog);
        d->egl.prog = 0;
    }

    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        /* egl.surface && egl.ctx are only created on x11, see
           spice_egl_init() */

        if (d->egl.surface != EGL_NO_SURFACE) {
            eglDestroySurface(d->egl.display, d->egl.surface);
            d->egl.surface = EGL_NO_SURFACE;
        }

        if (d->egl.ctx) {
            eglDestroyContext(d->egl.display, d->egl.ctx);
            d->egl.ctx = 0;
        }

        eglMakeCurrent(d->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);

        /* do not call eglterminate() since egl may be used by
         * somebody else code */
    }
}

G_GNUC_INTERNAL
void spice_egl_resize_display(SpiceDisplay *display, int w, int h)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    int prog;

    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

    glUseProgram(d->egl.prog);
    apply_ortho(d->egl.mproj, 0, w, 0, h, -1, 1);
    glViewport(0, 0, w, h);

    if (d->ready)
        spice_egl_update_display(display);

    glUseProgram(prog);
}

static void
draw_rect_from_arrays(SpiceDisplay *display, const void *verts, const void *tex)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    glBindBuffer(GL_ARRAY_BUFFER, d->egl.vbuf_id);

    if (verts) {
        glEnableVertexAttribArray(d->egl.attr_pos);
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        VERTS_ARRAY_SIZE,
                        verts);
        glVertexAttribPointer(d->egl.attr_pos, 4, GL_FLOAT,
                              GL_FALSE, 0, 0);
    }
    if (tex) {
        glEnableVertexAttribArray(d->egl.attr_tex);
        glBufferSubData(GL_ARRAY_BUFFER,
                        VERTS_ARRAY_SIZE,
                        TEX_ARRAY_SIZE,
                        tex);
        glVertexAttribPointer(d->egl.attr_tex, 2, GL_FLOAT,
                              GL_FALSE, 0,
                              (void *)VERTS_ARRAY_SIZE);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (verts)
        glDisableVertexAttribArray(d->egl.attr_pos);
    if (tex)
        glDisableVertexAttribArray(d->egl.attr_tex);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static GLvoid
client_draw_rect_tex(SpiceDisplay *display,
                     float x, float y, float w, float h,
                     float tx, float ty, float tw, float th)
{
    float verts[4][4];
    float tex[4][2];

    verts[0][0] = x;
    verts[0][1] = y;
    verts[0][2] = 0.0;
    verts[0][3] = 1.0;
    tex[0][0] = tx;
    tex[0][1] = ty;
    verts[1][0] = x + w;
    verts[1][1] = y;
    verts[1][2] = 0.0;
    verts[1][3] = 1.0;
    tex[1][0] = tx + tw;
    tex[1][1] = ty;
    verts[2][0] = x;
    verts[2][1] = y + h;
    verts[2][2] = 0.0;
    verts[2][3] = 1.0;
    tex[2][0] = tx;
    tex[2][1] = ty + th;
    verts[3][0] = x + w;
    verts[3][1] = y + h;
    verts[3][2] = 0.0;
    verts[3][3] = 1.0;
    tex[3][0] = tx + tw;
    tex[3][1] = ty + th;

    draw_rect_from_arrays(display, verts, tex);
}

G_GNUC_INTERNAL
void spice_egl_cursor_set(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    GdkPixbuf *image = d->mouse_pixbuf;

    g_return_if_fail(d->egl.enabled);

    if (image == NULL)
        return;

    int width = gdk_pixbuf_get_width(image);
    int height = gdk_pixbuf_get_height(image);

    glBindTexture(GL_TEXTURE_2D, d->egl.tex_pointer_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 gdk_pixbuf_read_pixels(image));
    glBindTexture(GL_TEXTURE_2D, 0);
}

G_GNUC_INTERNAL
void spice_egl_update_display(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    double s;
    int x, y, w, h;
    gdouble tx, ty, tw, th;
    int prog;

    g_return_if_fail(d->ready);

    spice_display_get_scaling(display, &s, &x, &y, &w, &h);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    tx = ((float)d->area.x / (float)d->egl.scanout.width);
    ty = ((float)d->area.y / (float)d->egl.scanout.height);
    tw = ((float)d->area.width / (float)d->egl.scanout.width);
    th = ((float)d->area.height / (float)d->egl.scanout.height);

    /* convert to opengl coordinates, 0 is bottom, 1 is top. ty should
     * be the bottom of the area, since th is upward */
    /* 1+---------------+ */
    /*  |               | */
    /*  |  ty  to  |th  | */
    /*  |  |   ->  |    | */
    /*  |  |th     ty   | */
    /*  |               | */
    /*  |               | */
    /*  +---------------+ */
    /* 0 */
    ty = 1 - (ty + th);


    /* if the scanout is inverted, then invert coordinates and direction too */
    if (!d->egl.scanout.y0top) {
        ty = 1 - ty;
        th = -1 * th;
    }
    SPICE_DEBUG("update %f +%d+%d %dx%d +%f+%f %fx%f", s, x, y, w, h,
                tx, ty, tw, th);

    glBindTexture(GL_TEXTURE_2D, d->egl.tex_id);
    glDisable(GL_BLEND);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glUseProgram(d->egl.prog);
    client_draw_rect_tex(display, x, y, w, h,
                         tx, ty, tw, th);

    if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER &&
        d->mouse_guest_x != -1 && d->mouse_guest_y != -1 &&
        !d->show_cursor &&
        spice_gtk_session_get_pointer_grabbed(d->gtk_session) &&
        d->mouse_pixbuf != NULL) {
        GdkPixbuf *image = d->mouse_pixbuf;
        int width = gdk_pixbuf_get_width(image);
        int height = gdk_pixbuf_get_height(image);

        glBindTexture(GL_TEXTURE_2D, d->egl.tex_pointer_id);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        client_draw_rect_tex(display,
                             x + (d->mouse_guest_x - d->mouse_hotspot.x) * s,
                             y + h - (d->mouse_guest_y - d->mouse_hotspot.y) * s,
                             width, -height,
                             0, 0, 1, 1);
    }

    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        /* gtk+ does the swap with gtkglarea */
        eglSwapBuffers(d->egl.display, d->egl.surface);
    }

    glUseProgram(prog);
}


G_GNUC_INTERNAL
gboolean spice_egl_update_scanout(SpiceDisplay *display,
                                  const SpiceGlScanout *scanout,
                                  GError **err)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    EGLint attrs[13];
    guint32 format;

    g_return_val_if_fail(scanout != NULL, FALSE);
    format = scanout->format;

    if (d->egl.image != NULL) {
        eglDestroyImageKHR(d->egl.display, d->egl.image);
        d->egl.image = NULL;
    }

    if (scanout->fd == -1)
        return TRUE;

    attrs[0] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attrs[1] = scanout->fd;
    attrs[2] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attrs[3] = scanout->stride;
    attrs[4] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attrs[5] = 0;
    attrs[6] = EGL_WIDTH;
    attrs[7] = scanout->width;
    attrs[8] = EGL_HEIGHT;
    attrs[9] = scanout->height;
    attrs[10] = EGL_LINUX_DRM_FOURCC_EXT;
    attrs[11] = format;
    attrs[12] = EGL_NONE;
    SPICE_DEBUG("fd:%d stride:%d y0:%d %dx%d format:0x%x (%c%c%c%c)",
                scanout->fd, scanout->stride, scanout->y0top,
                scanout->width, scanout->height, format,
                format & 0xff, (format >> 8) & 0xff, (format >> 16) & 0xff, format >> 24);

    d->egl.image = eglCreateImageKHR(d->egl.display,
                                       EGL_NO_CONTEXT,
                                       EGL_LINUX_DMA_BUF_EXT,
                                       (EGLClientBuffer)NULL,
                                       attrs);

    glBindTexture(GL_TEXTURE_2D, d->egl.tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)d->egl.image);
    d->egl.scanout = *scanout;

    return TRUE;
}
