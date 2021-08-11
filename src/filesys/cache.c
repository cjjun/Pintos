
#include <debug.h>
#include <round.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "devices/timer.h"

static const int mod = 1000000007;

struct buffer_cache {
    block_sector_t sector;
    bool dirty;
    struct lock lock;
    int AR, AW, WR, WW;
    struct condition ok_to_read, ok_to_write;
    uint8_t cache[BLOCK_SECTOR_SIZE];
    struct list_elem lelem;
    struct hash_elem helem;
};


static unsigned hash_func (const struct hash_elem *e, void *aux) 
{
    struct buffer_cache *cache = hash_entry(e, struct buffer_cache, helem);
    
    unsigned hash_code = ( (long long)cache->sector << 32) % mod;

    return hash_code;
}

static bool less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
    struct buffer_cache *c1 = hash_entry(a, struct buffer_cache, helem);
    struct buffer_cache *c2 = hash_entry(b, struct buffer_cache, helem);

    return c1->sector < c2->sector;
}

/* 
    Block buffer global list and table
*/
struct list sector_list;
struct hash sector_map;

struct lock table_lock;
struct lock evict_lock;

struct condition ok_to_evict;

struct buffer_cache *query_and_update (block_sector_t sector);
void backup (void *aux);

void block_buffer_init (uint32_t buffer_size)
{
    list_init (&sector_list);
    hash_init (&sector_map, hash_func, less_func, NULL);
    lock_init (&table_lock);
    lock_init (&evict_lock);
    cond_init (&ok_to_evict);

    /* Create list and table for buffer_size with dummy data. */
    struct buffer_cache *dummy = calloc (buffer_size, sizeof(struct buffer_cache));
    for (int i = 0; i < buffer_size; i++) {
        struct buffer_cache *sector = &dummy[i];
        sector->sector = i;
        sector->dirty = false;
        lock_init (&sector->lock);
        sector->AR = sector->AW = sector->WR = sector->WW = 0;
        cond_init (&sector->ok_to_read);
        cond_init (&sector->ok_to_write);

        block_read(fs_device, i, &sector->cache);
        list_push_back (&sector_list, &sector->lelem);
        hash_insert (&sector_map, &sector->helem);
    }

    /* Initialize autosaver*/
    // thread_create ("Backup", PRI_DEFAULT, backup, (void *)50);
}

struct buffer_cache *query_and_update (block_sector_t sector)
{
    struct buffer_cache tmp, *target;
    tmp.sector = sector;
    // Search for target sector in list.
    lock_acquire (&table_lock);
    struct hash_elem *it = hash_find (&sector_map, &tmp.helem);
    // If it's cached in list
    if (it != NULL) {
        target = hash_entry (it, struct buffer_cache, helem);
        if (&target->lelem != list_begin (&sector_list)) {
            list_remove (&target->lelem);
            list_push_front (&sector_list, &target->lelem);
        }
        lock_release (&table_lock);
        // printf("in: %d\n", target->sector);
        return target;

    } else {
        lock_release (&table_lock);

        // Ready to evict the last cache
        lock_acquire (&evict_lock);
        lock_acquire (&table_lock);
        while (true) {
            // Check if last element is capable of being evicted.
            target = list_entry (list_back (&sector_list), struct buffer_cache, lelem);
            lock_release (&table_lock);

            // For the last sector, wait until it can be evicted.
            lock_acquire (&target->lock);

            lock_acquire (&table_lock);
            if (target->AW + target->AR + target->WW + target->WR > 0) {
                // Wait until all writers and readers finish work.
                lock_release (&target->lock);
                cond_wait (&ok_to_evict, &table_lock);
            }
            else {
                // Ready to evict.
                if (target->dirty)
                    block_write (fs_device, target->sector, target->cache);
                list_remove (&target->lelem);
                hash_delete (&sector_map, &target->helem);
                
                // Reuse the last element and reinitialize with new data
                target->sector = sector;
                target->dirty = false;
                lock_init (&target->lock);
                target->AR = target->AW = target->WR = target->WW = 0;
                cond_init (&target->ok_to_read);
                cond_init (&target->ok_to_write);

                list_push_front (&sector_list, &target->lelem);
                hash_insert (&sector_map, &target->helem);
                block_read(fs_device, sector, target->cache);

                lock_release (&table_lock);
                lock_release (&evict_lock);

                // printf("not: %d\n", target->sector);
                return target;   
            }
        }
    }
    
}

void block_buffer_read (block_sector_t sector, void *buffer)
{
    // printf("r: %d\n", sector);
    struct buffer_cache *cache = query_and_update (sector);
    lock_acquire (&cache->lock);
    while (cache->AW + cache->WW > 0) {
        cache->WR++;
        cond_wait (&cache->ok_to_read, &cache->lock);
        cache->WR--;
    }
    cache->AR++;
    lock_release (&cache->lock);

    memcpy(buffer, cache->cache, BLOCK_SECTOR_SIZE);

    lock_acquire (&cache->lock);
    cache->AR--;
    if (cache->AR == 0 && cache->WW > 0)
        cond_signal (&cache->ok_to_write, &cache->lock);
    
    if (cache->AR + cache->WR + cache->AW + cache->WW == 0) {
        lock_acquire (&table_lock);
        if (list_back(&sector_list) == &cache->lelem) {
            cond_signal (&ok_to_evict, &table_lock);
        }
        lock_release (&table_lock);
    }
    lock_release (&cache->lock);
}

void block_buffer_write (block_sector_t sector, void *buffer)
{
    // printf("w: %d\n", sector);
    struct buffer_cache *cache = query_and_update (sector);
    lock_acquire (&cache->lock);
    while (cache->AW + cache->AR > 0) {
        cache->WW++;
        cond_wait (&cache->ok_to_write, &cache->lock);
        cache->WW--;
    }
    cache->AW++;
    cache->dirty = true;
    lock_release (&cache->lock);

    memcpy (cache->cache, buffer, BLOCK_SECTOR_SIZE);

    lock_acquire (&cache->lock);
    cache->AW--;

    if(cache->WW > 0)
        cond_signal (&cache->ok_to_write, &cache->lock);
    else if (cache->WR > 0)
        cond_broadcast (&cache->ok_to_read, &cache->lock);
    
    if (cache->AR + cache->WR + cache->AW + cache->WW == 0) {
        lock_acquire (&table_lock);
        if (list_back(&sector_list) == &cache->lelem) {
            cond_signal (&ok_to_evict, &table_lock);
        }
        lock_release (&table_lock);
    }
    lock_release (&cache->lock);
}

void cache_save (void)
{
    lock_acquire (&table_lock);
    struct list_elem *it;
    for (it = list_begin (&sector_list); it != list_end (&sector_list); it = list_next (it)) {
        struct buffer_cache *target = list_entry (it, struct buffer_cache, lelem);
        if (target->dirty) {
            // printf("Save sector %d\n", target->sector);
            block_write (fs_device, target->sector, target->cache);
            if (lock_try_acquire (&target->lock) ) {
                if (target->AW == 0) {
                    target->dirty = false;
                }
                lock_release (&target->lock);
            }
        }
    }
    lock_release (&table_lock);
}

void backup (void *aux) 
{
    uint32_t ticks = (uint32_t)aux;
    while (true) {
        timer_sleep (ticks);
        cache_save ();
    }
}