#ifndef VM_FRAME_H
#define VN_FRAME_H

#include "threads/thread.h"

void init_frame_table (void);
uint32_t *frame_to_pte (void *frame_addr);

void install_frame (uint32_t *target_pte);
void free_frame (uint32_t *pte);
void pin_frame (void *stack, void *code);

#endif /* vm/frame.h */