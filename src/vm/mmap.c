#include <round.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/mmap.h"
#include "vm/page.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"


bool mmap (struct file *file, off_t ofs, void *upage, uint32_t read_bytes, bool writable) {
    
    if (pg_ofs (upage) != 0 || file == NULL || upage == NULL)
        return false;
    
    ASSERT ( (off_t)read_bytes + ofs <= file_length (file));
    int page_cnt = DIV_ROUND_UP (read_bytes, PGSIZE);
    
    void *cur = upage;
    for(int i = 0; i < page_cnt; ++i) {
        if( page_is_valid (cur) )
            return false;
        uint32_t *pte = get_virtual_page (cur, writable);
        if (pte == NULL)
            return false;
        set_page_mmap (pte, file, ofs + cur - upage, read_bytes > PGSIZE? PGSIZE: read_bytes);
        cur += PGSIZE;
        read_bytes -= PGSIZE;
    }

    return true;
}

bool load_seg_mmap (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
    ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT (pg_ofs (upage) == 0);
    ASSERT (ofs % PGSIZE == 0);

    while (read_bytes > 0 || zero_bytes > 0) 
    {
        /* Calculate how to fill this page.
            We will read PAGE_READ_BYTES bytes from FILE
            and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* Get a page of memory. */
        uint32_t *pte = get_virtual_page (upage, writable);
        if (pte == NULL)
            return false;

        if (page_read_bytes)
            set_page_mmap (pte, file, ofs, page_read_bytes);

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
        ofs += page_read_bytes;
    }
    return true;
}

void munmap (void *upage, uint32_t mmap_length) {

    int page_cnt = DIV_ROUND_UP (mmap_length, PGSIZE);

    for(int i = 0; i < page_cnt; ++i) {
        ASSERT ( page_is_valid (upage) );
        page_virtual_free ( uaddr_to_pte (upage) );
        upage += PGSIZE;
    }
}