#include <stdio.h>
#include <bitmap.h>
#include <round.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include "vm/page.h"

static struct block *block_swap;
static block_sector_t swap_sectors;
static struct bitmap *bitmap;

struct lock swap_lock;

size_t swap_get_sectors (block_sector_t sector_cnt);
 
void swap_init (void) {

    block_swap = block_get_role (BLOCK_SWAP);
    if (block_swap == NULL) 
        PANIC ("Cannot open block swap!\n");
    swap_sectors = block_size (block_swap);
    lock_init (&swap_lock);
    printf("swap lock %p\n", &swap_lock);
    /* Initialize bit map */
    size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (swap_sectors), PGSIZE);
    if (bm_pages > swap_sectors)
        PANIC ("Not enough memory in swap for bitmap.");
    void *bm_addr = palloc_get_multiple (0, bm_pages);
    bitmap = bitmap_create_in_buf (swap_sectors, bm_addr, bm_pages * PGSIZE);
    printf ("%u sectors available in swap.\n", swap_sectors);
}

/* Check and get sector_cnt continuous sectors and return the sector_index for the
    first sector. If it does not exist, return BITMAP_ERROR. */
size_t swap_get_sectors (block_sector_t sector_cnt) {
    
    if (sector_cnt == 0)
        return BITMAP_ERROR;
    
    lock_acquire (&swap_lock);
    size_t sector_idx = bitmap_scan_and_flip (bitmap, 0, sector_cnt, false);
    lock_release (&swap_lock);

    return sector_idx;
}

/* Find and store data in consecutive sectors. If no enough sectors, PANIC the kernel.  */
block_sector_t swap_in (void *addr, size_t write_bytes) {
    
    block_sector_t sector_cnt =  DIV_ROUND_UP (write_bytes, BLOCK_SECTOR_SIZE);

    size_t bitmap_idx = swap_get_sectors (sector_cnt);
    if (bitmap_idx == BITMAP_ERROR)
        PANIC ("Not enough memory in swap for page swap");
    block_sector_t sector_idx = (block_sector_t) bitmap_idx;
    
    for (uint32_t i = 0; i < sector_cnt; i++) {
        block_write (block_swap, sector_idx + i, addr);
        addr += BLOCK_SECTOR_SIZE;
    }

    return sector_idx;
}

/* Read continuous read_bytes bytes from SWAP given initial sector index */
void swap_out (void *addr, block_sector_t sector_idx, size_t read_bytes) {

    block_sector_t sector_cnt =  DIV_ROUND_UP (read_bytes, BLOCK_SECTOR_SIZE);

    lock_acquire (&swap_lock);
    ASSERT (bitmap_all (bitmap, sector_idx, sector_cnt));
    lock_release (&swap_lock);

    for (uint32_t i = 0; i < sector_cnt; i++) {
        block_read (block_swap, sector_idx + i, addr);
        addr += BLOCK_SECTOR_SIZE;
    }
    /* Free sector. */
    lock_acquire (&swap_lock);
    bitmap_set_multiple (bitmap, sector_idx, sector_cnt, false);
    lock_release (&swap_lock);
    // printf("%u  ", hash_page (addr - PGSIZE));
}

/* Free sectors from SWAP. */
void swap_free (block_sector_t sector_idx, size_t free_size) {

    block_sector_t sector_cnt =  DIV_ROUND_UP (free_size, BLOCK_SECTOR_SIZE);
    
    lock_acquire (&swap_lock);
    ASSERT (bitmap_all (bitmap, sector_idx, sector_cnt));
    bitmap_set_multiple (bitmap, sector_idx, sector_cnt, false);
    lock_release (&swap_lock);
}