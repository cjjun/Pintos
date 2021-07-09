#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"

void swap_init (void);
block_sector_t swap_in (void *addr, size_t write_bytes);
void swap_out (void *addr, block_sector_t sector_idx, size_t read_bytes);
void swap_free (block_sector_t sector_idx, size_t free_size);

#endif /* vm/swap.h */