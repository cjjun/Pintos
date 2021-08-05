#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "threads/init.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

struct hash hash_page_table;

#define ll long long
static const int mod = 1000000007;
/* Guard for hash_page_table */
struct lock lock;

/*
    Define important hash table functions
*/
static unsigned hash_func (const struct hash_elem *e, void *aux) {
    struct sup_page_table *table = hash_entry(e, struct sup_page_table, elem);
    ll code = (ll)table->pte % mod;

    return (unsigned)code;
}

static bool less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
    struct sup_page_table *t1 = hash_entry(a, struct sup_page_table, elem);
    struct sup_page_table *t2 = hash_entry(b, struct sup_page_table, elem);

    return (unsigned)t1->pte < (unsigned)t2->pte;
}

/*
    Initialize a supplementary page table. It includes initialize the lock
and initialize the hash table */
void init_hash_spt (void) {
    lock_init (&lock);
    hash_init (&hash_page_table, hash_func, less_func, NULL);
}

/*
    Restore a page stored in swap.
    1. Pick and load a new frame.
    2. Retrive the sector that cached in swap.
    3. Read the sectors and write directly to loaded frame.
    4. Unmark the swapped flag. */

void load_from_swap (uint32_t *pte) {
    ASSERT (pte && !pte_is_present (pte));
    ASSERT ( pte_is_swapped (pte) );

    install_frame (pte);
    void *kpage = pte_to_kpage (pte);

    struct sup_page_table *table = lookup_spt (pte);

    block_sector_t sector_idx = table->swap_sector;

    swap_out (kpage, sector_idx, PGSIZE);
    // printf("%p load from sector %d, %d\n", pte, sector_idx, hash_page (kpage));

    pte_set_swap (pte, false);
}

/* Initialize a page with file information stored in SPT
    1. Lookup SPT from PTE and get the [file:offset]
    2. Get the virtual frame and read from file.
    Note this function won't check or change flag information.
*/
bool load_from_filesys (uint32_t *pte) {

    ASSERT (pte && pte_is_present (pte));
    
    struct sup_page_table *table = lookup_spt (pte);
    ASSERT (table != NULL);

    struct file *file_ptr = table->file_ptr;
    off_t offset = table->offset;
    ASSERT (file_ptr);

    uint32_t read_bytes = table->read_bytes;

    file_seek (file_ptr, offset);
    off_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    off_t page_zero_bytes = PGSIZE - page_read_bytes;

    void *kpage = pte_to_kpage (pte);
    memset (kpage, 0, PGSIZE);

    acquire_filesys_lock();
    if ( (uint32_t)file_read (file_ptr, kpage, page_read_bytes) != page_read_bytes ) {
        release_filesys_lock ();
        return false;
    }
    release_filesys_lock ();

    return true;
}

/*
    Store the page in SWAP and free the page.
    1. Store the kpage in swap and return index of the first sector.
    2. Store the sector index in supplementary page table.
    3. Free the frame and uninstall from page.
    4. Mark the swapped flag. */
bool store_in_swap (uint32_t *pte) {
    
    ASSERT (pte && pte_is_present (pte));

    void *kpage = pte_to_kpage (pte);

    struct sup_page_table *table = lookup_spt (pte);

    block_sector_t sector_idx = swap_in (kpage, PGSIZE);
    // printf("%p store in sector %d %d\n", pte, sector_idx, hash_page (kpage));
    table->swap_sector = sector_idx;

    pte_uninstall_frame (pte);
    pte_set_swap (pte, true);

    return true;
}
/*
    If page is not mmapped, do nothing. If pte present,
    write data in frame to file.
*/
bool page_dirty_write (uint32_t *pte) {
    if (pte == NULL || !pte_is_dirty (pte))
        return false;

    struct sup_page_table *table = lookup_spt (pte);
    if ( table->file_ptr == NULL )
        return false;
    struct file *file = table->file_ptr;
    off_t offset = table->offset;

    if (file->deny_write)
        return false;
    /* Page is present */
    if (pte_is_present (pte)) {
        acquire_filesys_lock ();
        off_t write_bytes = table->read_bytes;

        void *kpage = pte_to_kpage (pte);
        write_bytes = write_bytes == file_write_at (file, kpage, write_bytes, offset );
        release_filesys_lock ();
        if (write_bytes > 0)
            return true;
    }
    return false;
}

/*  Get a virtual page by marking it valid and set initial parameters.
    1. Create page table entry.
    2. Initialize PTE with default configuration (not present, valid, 
user mode, writable decided by parameter).
    3. Create supplementary page table , initialize with default setting. */
uint32_t *get_virtual_page (void *upage, bool writable) {

    struct thread *t = thread_current();
    uint32_t *pd = t->pagedir;
    /* Set PTE */
    ASSERT (pg_ofs (upage) == 0);
    ASSERT (is_user_vaddr (upage));
    ASSERT (pd != init_page_dir);

    if ( pagedir_get_page (pd, upage) != NULL )
        return NULL;
    uint32_t *pte = pagedir_set_virtual_page (pd, upage, writable);

    struct sup_page_table *table = (struct sup_page_table *)malloc (sizeof (struct sup_page_table));

    /* Initialize a table with default setting*/
    table->swap_sector = 0;
    table->file_ptr = NULL;
    table->offset = 0;
    table->pte = pte;
    table->read_bytes = 0;

    hash_insert (&hash_page_table, &table->elem);
    return pte;
}

/* Map [file: offset] to a page for consecutive readbytes bytes. */
void set_page_mmap (uint32_t *pte, struct file *file_ptr, off_t offset, uint32_t readbytes) {
    struct sup_page_table *table = lookup_spt (pte);
    ASSERT (table != NULL);

    pte_set_mmap (pte, true);
    
    table->file_ptr = file_ptr;
    table->offset = offset;
    table->read_bytes = readbytes;
}

bool page_is_valid (void *uaddr) {

    uint32_t *pd = thread_current()->pagedir;

    return pagedir_is_valid (pd, uaddr);
}

/*  Recycle a virtual page, depending on following states:
    1. Page has been mapped from a file. Then we use page_dirty_write.
    2. Page is valid and present. Free the frame
    3. Page is valid but currently swapped. Simply free the swap
    and dischard everything.
    4. Page is valid and never initialized. Do nothing.

Common part: Clear pte, remove spt, and recycle its space.
*/
bool page_virtual_free (uint32_t *pte) {

    if (pte == NULL)
        return false;
    ASSERT (pte_is_valid (pte));
    struct sup_page_table *table = lookup_spt (pte);
    ASSERT (table);

    if (pte_is_present (pte)) {
        if (table->file_ptr != NULL) {
            if (pte_is_dirty (pte))
                page_dirty_write (pte);
        }
        free_frame (pte);
    } else {
        if ( pte_is_swapped (pte) )
            swap_free (table->swap_sector, PGSIZE);
    }
    *pte = 0;

    /* Remove from hash table */
    lock_acquire (&lock);
    hash_delete (&hash_page_table, &table->elem);
    lock_release (&lock);
    free (table);

    return true;
}

void page_absent_action (void *upage) {

    ASSERT (pg_ofs (upage) == 0);
    
    uint32_t *pte = uaddr_to_pte (upage);
    ASSERT (pte && pte_is_valid (pte));
    /* Case 0: present*/
    if (pte_is_present (pte))
        return;
    /* Case 1: swapped */
    if (pte_is_swapped (pte)) {
        load_from_swap (pte);
    }
    else {
        install_frame (pte);
        if (pte_is_mmapped (pte)) {
            if (!load_from_filesys (pte) )
                PANIC ("Filesys error\n");
            pte_set_mmap (pte, false);
        }
    }
}

/* Looks for supplementary page table by pte from hash table. */
struct sup_page_table *lookup_spt (uint32_t *pte) {
    struct sup_page_table tmp;
    tmp.pte = pte;
    lock_acquire (&lock);
    struct hash_elem * it = hash_find (&hash_page_table, &tmp.elem);
    lock_release (&lock);
    if (it == NULL)
        return NULL;
    else 
        return hash_entry(it, struct sup_page_table, elem);
}

uint32_t *uaddr_to_pte (void *uaddr) {
    return lookup_pte (thread_current()->pagedir, uaddr);
}

/* Find the virtual frame address linked to a page. */
void *pte_to_kpage (uint32_t *pte) {
    ASSERT (pte != NULL && pte_is_present (pte));
    void *phy_addr = pg_round_down ( (void *)*pte);
    ASSERT (phy_addr != NULL)
    return ptov ( (uintptr_t) phy_addr );
}