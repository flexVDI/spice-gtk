/* spice/common */
#include "ring.h"

typedef struct display_cache_item {
    RingItem                    hash_link;
    RingItem                    lru_link;
    uint64_t                    id;
    uint32_t                    refcount;
    void                        *ptr;
} display_cache_item;

typedef struct display_cache {
    const char                  *name;
    Ring                        hash[64];
    Ring                        lru;
    int                         nitems;
} display_cache;

static inline void cache_init(display_cache *cache, const char *name)
{
    int i;

    cache->name = name;
    ring_init(&cache->lru);
    for (i = 0; i < SPICE_N_ELEMENTS(cache->hash); i++) {
        ring_init(&cache->hash[i]);
    }
}

static inline Ring *cache_head(display_cache *cache, uint64_t id)
{
    return &cache->hash[id % SPICE_N_ELEMENTS(cache->hash)];
}

static inline void cache_used(display_cache *cache, display_cache_item *item)
{
    ring_remove(&item->lru_link);
    ring_add(&cache->lru, &item->lru_link);
}

static inline display_cache_item *cache_get_lru(display_cache *cache)
{
    display_cache_item *item;
    RingItem *ring;

    if (ring_is_empty(&cache->lru))
        return NULL;
    ring = ring_get_tail(&cache->lru);
    item = SPICE_CONTAINEROF(ring, display_cache_item, lru_link);
    return item;
}

static inline display_cache_item *cache_find(display_cache *cache, uint64_t id)
{
    display_cache_item *item;
    RingItem *ring;
    Ring *head;

    head = cache_head(cache, id);
    for (ring = ring_get_head(head); ring != NULL; ring = ring_next(head, ring)) {
        item = SPICE_CONTAINEROF(ring, display_cache_item, hash_link);
        if (item->id == id) {
            return item;
        }
    }
#if 0
    fprintf(stderr, "%s: %s %" PRIx64 " [not found]\n", __FUNCTION__,
            cache->name, id);
#endif
    return NULL;
}

static inline display_cache_item *cache_add(display_cache *cache, uint64_t id)
{
    display_cache_item *item;

    item = spice_new0(display_cache_item, 1);
    item->id = id;
    item->refcount = 1;
    ring_add(cache_head(cache, item->id), &item->hash_link);
    ring_add(&cache->lru, &item->lru_link);
    cache->nitems++;
#if 0
    fprintf(stderr, "%s: %s %" PRIx64 " (%d)\n", __FUNCTION__,
            cache->name, id, cache->nitems);
#endif
    return item;
}

static inline void cache_del(display_cache *cache, display_cache_item *item)
{
#if 0
    fprintf(stderr, "%s: %s %" PRIx64 "\n", __FUNCTION__,
            cache->name, item->id);
#endif
    ring_remove(&item->hash_link);
    ring_remove(&item->lru_link);
    free(item);
    cache->nitems--;
}

static inline void cache_ref(display_cache_item *item)
{
    item->refcount++;
}

static inline int cache_unref(display_cache_item *item)
{
    item->refcount--;
    return item->refcount == 0;
}
