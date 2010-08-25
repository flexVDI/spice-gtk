#include "spice-client.h"
#include "spice-common.h"

struct spice_watch {
    GIOChannel        *ch;
    guint             rd,wr;
    spice_watch_func  func;
    void              *opaque;
};

struct spice_timer {
};

static gboolean watch_rd(GIOChannel *source, GIOCondition condition,
                         gpointer data)
{
    struct spice_watch *watch = data;
    watch->func(SPICE_WATCH_EVENT_READ, watch->opaque);
    return TRUE;
}

static gboolean watch_wr(GIOChannel *source, GIOCondition condition,
                         gpointer data)
{
    struct spice_watch *watch = data;
    watch->func(SPICE_WATCH_EVENT_WRITE, watch->opaque);
    return TRUE;
}

spice_watch *spice_watch_new(int fd, int mask, spice_watch_func func, void *opaque)
{
    spice_watch *watch;

    watch = spice_new0(spice_watch, 1);
    watch->ch = g_io_channel_unix_new(fd);
    if (mask & SPICE_WATCH_EVENT_READ)
        watch->rd = g_io_add_watch(watch->ch, G_IO_IN, watch_rd, watch);
    if (mask & SPICE_WATCH_EVENT_WRITE)
        watch->wr = g_io_add_watch(watch->ch, G_IO_IN, watch_wr, watch);
    watch->func = func;
    watch->opaque = opaque;
    return watch;
}

void spice_watch_put(spice_watch *watch)
{
    if (watch->rd)
        g_source_destroy(g_main_context_find_source_by_id
                         (g_main_context_default(), watch->rd));
    if (watch->wr)
        g_source_destroy(g_main_context_find_source_by_id
                         (g_main_context_default(), watch->wr));
    free(watch);
}

spice_timer *spice_timer_new(spice_timer_func func, void *opaque)
{
    /* to be implemented */
    return NULL;
}

void spice_timer_put(spice_timer *timer)
{
}
