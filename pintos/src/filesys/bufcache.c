#include <debug.h>
#include <string.h>
#include "filesys/bufcache.h"
#include "threads/synch.h"
#include "filesys/filesys.h"

struct bufcache_entry {
    block_sector_t sector;
    struct list_elem lru_elem;
    struct condition until_ready;
    bool ready;
    bool dirty;
    uint8_t data[BLOCK_SECTOR_SIZE];
};


#define NUM_ENTRIES 64
#define INVALID_SECTOR 0

struct bufcache{
    struct bufcache_entry entries[NUM_ENTRIES];
    struct lock cache_lock;
    struct list lru_list;
    int num_ready;
    struct condition until_one_ready;
};

/* the buffer cache maintained by OS */
static struct bufcache bufcache;

/* internal helper functions */
static struct bufcache_entry* get_eviction_candidate(void);
static struct bufcache_entry* find(block_sector_t sector);
static void clean(struct bufcache_entry* entry);
static void replace(struct bufcache_entry* entry, block_sector_t sector);
static struct bufcache_entry* bufcache_access(block_sector_t sector, bool blind);


void bufcache_init(void)
{
    list_init(& (bufcache.lru_list));
    lock_init(& (bufcache.cache_lock));
    cond_init(& (bufcache.until_one_ready));
    bufcache.num_ready = NUM_ENTRIES;
    for(int i = 0; i < NUM_ENTRIES; i++){
        cond_init(& (bufcache.entries[i].until_ready));
        bufcache.entries[i].dirty = false;
        bufcache.entries[i].ready = true;
        bufcache.entries[i].sector = INVALID_SECTOR;
        list_push_front(&(bufcache.lru_list), &(bufcache.entries[i].lru_elem));
    }
}


static struct bufcache_entry* get_eviction_candidate(void){
    ASSERT(lock_held_by_current_thread(&bufcache.cache_lock));
    if(bufcache.num_ready == 0){
        return NULL;
    }
    struct bufcache_entry* candidate = list_entry(list_back(&(bufcache.lru_list)), struct bufcache_entry, lru_elem);
    /* entry farthest back in the lru_list where ready == true */
    while(candidate->ready == false){
        candidate = list_entry(list_prev(&(candidate->lru_elem)), struct bufcache_entry, lru_elem);
    }
    return candidate;
}

static struct bufcache_entry* find(block_sector_t sector)
{
    for(int i = 0; i < NUM_ENTRIES; i++){
        if(bufcache.entries[i].sector == sector){
            return &(bufcache.entries[i]);
        }
    }
    return NULL;
}

static void clean(struct bufcache_entry* entry)
{
    ASSERT(lock_held_by_current_thread(&(bufcache.cache_lock)));
    ASSERT(entry->dirty);
    entry->ready = false;
    bufcache.num_ready--;
    lock_release(&(bufcache.cache_lock));

    block_write(fs_device, entry->sector , &(entry->data)); //shouldn't be a problem; just following the skeleton code
    lock_acquire(&(bufcache.cache_lock));
    entry->ready = true;
    bufcache.num_ready++;
    entry->dirty = false;
    cond_broadcast(&entry->until_ready, &(bufcache.cache_lock));
    cond_broadcast(&bufcache.until_one_ready, &(bufcache.cache_lock));
}

static void replace(struct bufcache_entry* entry, block_sector_t sector)
{
    ASSERT(lock_held_by_current_thread(&bufcache.cache_lock));
    ASSERT(!entry->dirty);
    entry->sector = sector;
    entry->ready = false;
    bufcache.num_ready--;
    lock_release(&bufcache.cache_lock);
    
    /* read from disk */
    block_read(fs_device, sector, &(entry->data));
    
    lock_acquire(&bufcache.cache_lock);
    entry->ready = true;
    bufcache.num_ready++;
    cond_broadcast(&entry->until_ready, &(bufcache.cache_lock));
    cond_broadcast(&bufcache.until_one_ready, &(bufcache.cache_lock));
}

static struct bufcache_entry* bufcache_access(block_sector_t sector, bool blind)
{
    ASSERT(lock_held_by_current_thread(&bufcache.cache_lock));
    while(true){
        struct bufcache_entry* match = find(sector);
        if(match != NULL){
            if(!match->ready){
                cond_wait(&match->until_ready, &bufcache.cache_lock);
                continue;
            }
            /* move match to front */
            list_remove(&(match->lru_elem));
            list_push_front(&(bufcache.lru_list), &(match->lru_elem));
            return match;
        }
        struct bufcache_entry* to_evict = get_eviction_candidate();
        if(to_evict == NULL){
            cond_wait(&bufcache.until_one_ready, &bufcache.cache_lock);
        }else if (to_evict->dirty){
            clean(to_evict);
        }else if (blind){
            to_evict->sector = sector;
            /* on next iteration, find() should succeed */
        } else {
            replace(to_evict, sector);
        }
    } // end while
}


/* external API */
void bufcache_read (block_sector_t sector, void* buffer, size_t offset, size_t length)
{
    ASSERT(offset + length <= BLOCK_SECTOR_SIZE);
    lock_acquire(&bufcache.cache_lock);
    struct bufcache_entry* entry = bufcache_access(sector, false);
    memcpy(buffer, &entry->data[offset], length);
    lock_release(&bufcache.cache_lock);
}

void bufcache_write(block_sector_t sector, const void* buffer, size_t offset, size_t length)
{
    ASSERT(offset + length <= BLOCK_SECTOR_SIZE);
    lock_acquire(&bufcache.cache_lock);
    struct bufcache_entry* entry = bufcache_access(sector, length == BLOCK_SECTOR_SIZE);
    memcpy(&entry->data[offset], buffer, length);
    entry->dirty = true;
    lock_acquire(&bufcache.cache_lock);lock_acquire(&bufcache.cache_lock);
}

void bufcache_flush(void)
{
    lock_acquire(&bufcache.cache_lock);
    for(int i = 0; i < NUM_ENTRIES; i++){
        if (bufcache.entries[i].dirty) clean(&bufcache.entries[i]);
    }
    lock_acquire(&bufcache.cache_lock);
}