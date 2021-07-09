#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

/* 
    (FILESYS | IDLE) ---> READY <====> SWAP 
                          
*/

struct sup_page_table {
    uint32_t *pte;
    // uint32_t *pd;
    block_sector_t swap_sector;
    struct file *file_ptr;
    off_t offset;
    uint32_t read_bytes;
    struct hash_elem elem;
};

/* Initialize supplement hash page table*/
void init_hash_spt (void);

/* Page load/store operations */
void load_from_swap (uint32_t *pte);
bool load_from_filesys (uint32_t *pte);
bool store_in_swap (uint32_t *pte);
bool page_dirty_write (uint32_t *pte);

/* Virtual page allocation to implement lazy load*/
uint32_t *get_virtual_page (void *upage, bool writable);
void set_page_mmap (uint32_t *pte, struct file *file_ptr, off_t offset, uint32_t readbytes);
bool page_is_valid (void *upage);
bool page_virtual_free (uint32_t *pte);
void page_absent_action (void *upage);

/* Return Supplement page table for pte */
struct sup_page_table *lookup_spt (uint32_t *pte);
uint32_t *uaddr_to_pte (void *uaddr);
void *pte_to_kpage (uint32_t *pte);

#endif /* vm/page.h */