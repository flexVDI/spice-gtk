#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "tinyjpeg.h"
#include "va_display_x11.h"
#include <va/va_x11.h>
#include "spice-util.h"

static GdkWindow *spice_toplevel_window;

void va_x11_set_toplevel_window(void *window)
{
    SPICE_DEBUG("VA X11 setting toplevel window to %p", window);
    spice_toplevel_window = (GdkWindow *)window;
}

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

static void window_clicked_cb(GtkWidget *widget, void *e)
{
    GdkEventButton *event = e;
    GdkEventKey ke;

    SPICE_DEBUG("window_clicked_cb: rel %f,%f, abs %f,%f, toplevel win %p",
                event->x, event->y, event->x_root, event->y_root, spice_toplevel_window);

    ke.type = GDK_KEY_PRESS;
    ke.window = spice_toplevel_window;
    ke.time = GDK_CURRENT_TIME;
    ke.keyval = event->x_root;
    ke.hardware_keycode = event->y_root;
    ke.group = 69;
    ke.state = 0;
    ke.length = 0;
    ke.string = 0;
    ke.send_event = 0;

    gdk_event_put((GdkEvent *)&ke);
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
        gtk_window_set_default_size(GTK_WINDOW(widget), dst_rect->width, dst_rect->height);
        gtk_window_move(GTK_WINDOW(widget), dst_rect->x, dst_rect->y);
        gtk_window_set_decorated(GTK_WINDOW(widget), FALSE);
        gtk_window_set_accept_focus(GTK_WINDOW(widget), FALSE);
        gtk_window_set_keep_above(GTK_WINDOW(widget), TRUE);
        g_signal_connect(G_OBJECT(widget), "button_release_event",
                         G_CALLBACK(window_clicked_cb), session);
        gtk_widget_add_events(widget, GDK_BUTTON_RELEASE_MASK);
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

