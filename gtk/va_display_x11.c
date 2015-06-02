#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "tinyjpeg.h"
#include <va/va_x11.h>

struct display_private {
    Display *x11_dpy;
    Window x11_win;
    GtkWidget *gtk_widget;
};

static VAStatus va_x11_open_display(tinyjpeg_session *session)
{
    Display *x11_dpy = XOpenDisplay(NULL);
    VAStatus va_status = VA_STATUS_ERROR_UNKNOWN;
    if (x11_dpy) {
        session->va_dpy = vaGetDisplay(x11_dpy);
        int major, minor;
        va_status = vaInitialize(session->va_dpy, &major, &minor);
        if (va_status == VA_STATUS_SUCCESS) {
            session->dpy_priv = malloc(sizeof(struct display_private));
            memset(session->dpy_priv, 0, sizeof(struct display_private));
            session->dpy_priv->x11_dpy = x11_dpy;
        }
    }
    return va_status;
}

static void va_x11_close_display(tinyjpeg_session *session)
{
    if (session->dpy_priv) {
        gtk_widget_destroy(session->dpy_priv->gtk_widget);
        XCloseDisplay(session->dpy_priv->x11_dpy);
        free(session->dpy_priv);
    }
}

static VAStatus va_x11_put_surface(tinyjpeg_session *session, VASurfaceID surface)
{
    if (!session->dpy_priv)
        return VA_STATUS_ERROR_INVALID_DISPLAY;
    if (!session->va_dpy)
        return VA_STATUS_ERROR_INVALID_DISPLAY;
    if (surface == VA_INVALID_SURFACE)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    GtkWidget *widget = session->dpy_priv->gtk_widget;
    VARectangle *src_rect = &session->src_rect;
    VARectangle *dst_rect = &session->dst_rect;

    if (widget) {
        gtk_window_resize(GTK_WINDOW(widget), dst_rect->width, dst_rect->height);
        gtk_window_move(GTK_WINDOW(widget), dst_rect->x, dst_rect->y);
    } else {
        GdkWindow *gdk_window;
        Window x11_window;

        widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(widget), FALSE);
        gtk_window_set_accept_focus(GTK_WINDOW(widget), FALSE);
        gtk_window_set_keep_above(GTK_WINDOW(widget), TRUE);
        gtk_widget_show(widget);

        gdk_window = gtk_widget_get_window(widget);
        x11_window = GDK_WINDOW_XID(gdk_window);

        session->dpy_priv->gtk_widget = widget;
        session->dpy_priv->x11_win = x11_window;
    }

    return vaPutSurface(session->va_dpy, surface, session->dpy_priv->x11_win,
                        0, 0, src_rect->width, src_rect->height,
                        0, 0, dst_rect->width, dst_rect->height,
                        NULL, 0, VA_FRAME_PICTURE);
}

static VADisplayHooks va_x11_display_hooks = {
    .open_display = va_x11_open_display,
    .close_display = va_x11_close_display,
    .put_surface = va_x11_put_surface,
};

void va_x11_set_display_hooks(void)
{
    set_va_display_hooks(&va_x11_display_hooks);
}

