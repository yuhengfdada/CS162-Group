#include <debug.h>
#include <string.h>
#include "filesys/bufcache.h"
#include "threads/synch.h"
#include "filesys/filesys.h"

/* A buffer cache entry and its metadata. */
struct bufcache_entry {
    block_sector_t sector;
    struct list_elem lru_elem;
    struct condition until_ready;
    bool ready;
    bool dirty;
    uint8_t data[BLOCK_SECTOR_SIZE];
};

#define NUM_ENTRIES 64
#define INVALID_SECTOR 0xffff

/* A struct for the entire buffer cache. */
struct bufcache{
    struct bufcache_entry entries[NUM_ENTRIES];
    struct lock cache_lock;
    struct list lru_list;
    struct condition until_one_ready;
    unsigned num_ready; // Number of buffer cache entries that are ready
    int num_hits;       // Number of hits
    int num_accesses;   // Total number of accesses
};

/* The buffer cache maintained by OS. */
static struct bufcache bufcache;

/* Internal helper functions that assume the caller already holds the cache_lock. */
static struct bufcache_entry* get_eviction_candidate(void);
static struct bufcache_entry* find(block_sector_t sector);
static void clean(struct bufcache_entry* entry);
static void replace(struct bufcache_entry* entry, block_sector_t sector);
static struct bufcache_entry* bufcache_access(block_sector_t sector, bool blind);

/* Initialize the entire buffer cache by initializing all the locks and conditional variables. */
void bufcache_init(void)
{
    list_init(& (bufcache.lru_list));
    lock_init(& (bufcache.cache_lock));
    cond_init(& (bufcache.until_one_ready));
    bufcache.num_ready = NUM_ENTRIES;
    bufcache.num_hits = 0;
    bufcache.num_accesses = 0;
    for(int i = 0; i < NUM_ENTRIES; i++){
        cond_init(& (bufcache.entries[i].until_ready));
        bufcache.entries[i].dirty = false;
        bufcache.entries[i].ready = true;
        bufcache.entries[i].sector = INVALID_SECTOR;
        list_push_front(&(bufcache.lru_list), &(bufcache.entries[i].lru_elem));
    }
}

/* Return the last bufcache_entry in the list that is also ready. Otherwise, return NULL. */
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

/* Return a bufcache_entry with matching sector. Otherwise, return NULL. */
static struct bufcache_entry* find(block_sector_t sector)
{
    for(int i = 0; i < NUM_ENTRIES; i++){
        if(bufcache.entries[i].sector == sector){
            return &(bufcache.entries[i]);
        }
    }
    return NULL;
}

/* Write back an entry inside bufcache to the disk. */
static void clean(struct bufcache_entry* entry)
{
    ASSERT(lock_held_by_current_thread(&(bufcache.cache_lock)));
    ASSERT(entry->dirty);
    entry->ready = false;
    bufcache.num_ready--;
    lock_release(&(bufcache.cache_lock));

    /* Write to disk. */
    block_write(fs_device, entry->sector , &(entry->data));

    lock_acquire(&(bufcache.cache_lock));
    entry->ready = true;
    bufcache.num_ready++;
    entry->dirty = false;
    cond_broadcast(&entry->until_ready, &(bufcache.cache_lock));
    cond_broadcast(&bufcache.until_one_ready, &(bufcache.cache_lock));
}

/* Read an entry in bufcache from the disk. */
static void replace(struct bufcache_entry* entry, block_sector_t sector)
{
    ASSERT(lock_held_by_current_thread(&bufcache.cache_lock));
    ASSERT(!entry->dirty);
    entry->sector = sector;
    entry->ready = false;
    bufcache.num_ready--;
    lock_release(&bufcache.cache_lock);
    
    /* Read from disk */
    block_read(fs_device, sector, &(entry->data));
    
    lock_acquire(&bufcache.cache_lock);
    entry->ready = true;
    bufcache.num_ready++;
    cond_broadcast(&entry->until_ready, &(bufcache.cache_lock));
    cond_broadcast(&bufcache.until_one_ready, &(bufcache.cache_lock));
}

/* Look inside bufcache for an entry with matching sector, and this might involve eviction. */
static struct bufcache_entry* bufcache_access(block_sector_t sector, bool blind)
{
    ASSERT(lock_held_by_current_thread(&bufcache.cache_lock));
    bufcache.num_accesses += 1;
    bool is_hit = true;
    while(true){
        struct bufcache_entry* match = find(sector);
        if(match != NULL){
            if(!match->ready){
                cond_wait(&match->until_ready, &bufcache.cache_lock);
                continue;
            }
            /* Move match to front. */
            if (is_hit) {
                bufcache.num_hits += 1;
                is_hit = false;
            }
            list_remove(&(match->lru_elem));
            list_push_front(&(bufcache.lru_list), &(match->lru_elem));
            return match;
        }
        is_hit = false;
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
    }
}

/* The following three functions are external API. */
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
    lock_release(&bufcache.cache_lock);
}

void bufcache_flush(void)
{
    lock_acquire(&bufcache.cache_lock);
    for(int i = 0; i < NUM_ENTRIES; i++){
        if (bufcache.entries[i].dirty) {
            clean(&bufcache.entries[i]);
        }
    }
    lock_release(&bufcache.cache_lock);
}

int bufcache_hit_count(void) {
    return bufcache.num_hits;
}

int bufcache_access_count(void) {
    return bufcache.num_accesses;
}

void bufcache_reset(void) {
    bufcache.num_ready = NUM_ENTRIES;
    bufcache.num_hits = 0;
    bufcache.num_accesses = 0;
    for(int i = 0; i < NUM_ENTRIES; i++){
        bufcache.entries[i].dirty = false;
        bufcache.entries[i].ready = true;
        bufcache.entries[i].sector = INVALID_SECTOR;
    }
}
