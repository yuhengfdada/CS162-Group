#include "bufcache.h"
#include "threads/synch.h"

struct data {
    unsigned char contents[BLOCK_SECTOR SIZE];
};

struct metadata {
    block_sector_t sector;
    struct data* entry;
    struct list_elem lru elem;
    struct condition until_ready;
    bool ready;
    bool dirty;
};


#define NUM ENTRIES 64
static data cached_data [NUM_ENTRIES];
static metadata entries [NUM_ENTRIES];
static struct lock cache_lock;
static struct condition until_one_ready;
static struct list lru_list;

static struct metadata* get_eviction_candidate(void)
{
    ASSERT()
}


static struct metadata* find(block_sector_t sector);
static void clean(struct metadata* entry);
static void replace(struct metadata* entry, block_sector_t sector);
static struct metadata* bufcache_access(block_sector_t sector);








