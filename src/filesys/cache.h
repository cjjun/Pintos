#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "list.h"
#include "hash.h"
#include "devices/block.h"

void block_buffer_init (uint32_t buffer_size);
void block_buffer_read (block_sector_t sector, void *buffer);
void block_buffer_write (block_sector_t sector, void *buffer);
void auto_save (void);

#endif /* filesys/cache.h */
