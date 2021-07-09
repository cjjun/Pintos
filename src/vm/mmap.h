#ifndef VM_MMAP_H
#define VN_MMAP_H

#include <list.h>
#include "filesys/file.h"
/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

/* mmap struct */
struct mmap 
{
    mapid_t mapid;
    struct file *file;
    void *addr;
    off_t map_length;
    struct list_elem elem;
};

/* Mmap function definition. */
bool mmap (struct file *file, off_t ofs, void *upage, uint32_t read_bytes, bool writable);
bool load_seg_mmap (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable);
void munmap (void *upage, uint32_t mmap_length);

#endif /* vm/mmap.h */