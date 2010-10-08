#ifndef __SPICE_CLIENT_WIDGET_H__
#define __SPICE_CLIENT_WIDGET_H__

#include "spice-client.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SPICE_TYPE_DISPLAY            (spice_display_get_type())
#define SPICE_DISPLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), SPICE_TYPE_DISPLAY, SpiceDisplay))
#define SPICE_DISPLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), SPICE_TYPE_DISPLAY, SpiceDisplayClass))
#define SPICE_IS_DISPLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), SPICE_TYPE_DISPLAY))
#define SPICE_IS_DISPLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SPICE_TYPE_DISPLAY))
#define SPICE_DISPLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), SPICE_TYPE_DISPLAY, SpiceDisplayClass))


typedef struct _SpiceDisplay SpiceDisplay;
typedef struct _SpiceDisplayClass SpiceDisplayClass;
typedef struct spice_display spice_display;

struct _SpiceDisplay {
    GtkDrawingArea parent;
    spice_display *priv;
    /* Do not add fields to this struct */
};

struct _SpiceDisplayClass {
    GtkDrawingAreaClass parent_class;

    /* signals */
    void (*spice_display_mouse_grab)(SpiceChannel *channel, gint grabbed);

    /* Do not add fields to this struct */
};

GType	        spice_display_get_type(void);
G_END_DECLS

GtkWidget* spice_display_new(SpiceSession *session, int id);
void spice_display_mouse_ungrab(GtkWidget *widget);
void spice_display_copy_to_guest(GtkWidget *widget);
void spice_display_paste_from_guest(GtkWidget *widget);

#endif /* __SPICE_CLIENT_WIDGET_H__ */
