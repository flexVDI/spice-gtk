#ifndef __SPICE_CLIENT_EVENTS_H__
#define __SPICE_CLIENT_EVENTS_H__

typedef struct spice_watch spice_watch;
typedef struct spice_timer spice_timer;

#define SPICE_WATCH_EVENT_READ  (1 << 0)
#define SPICE_WATCH_EVENT_WRITE (1 << 1)

typedef void (*spice_watch_func)(int event, void *opaque);
typedef void (*spice_timer_func)(void *opaque);

spice_watch *spice_watch_new(int fd, int mask, spice_watch_func func, void *opaque);
void spice_watch_put(spice_watch *watch);

spice_timer *spice_timer_new(spice_timer_func func, void *opaque);
void spice_timer_put(spice_timer *timer);

#endif /* __SPICE_CLIENT_EVENTS_H__ */
