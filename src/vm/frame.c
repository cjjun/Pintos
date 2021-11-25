#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <round.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "vm/mmap.h"
#include "vm/page.h"
#include "vm/frame.h"

static uint32_t clock_hand;
size_t frame_num;

uint32_t **frame_table;
void *base, *end_addr;
struct lock frame_lock;

uint32_t *pin_stack_pte, *pin_code_pte;

uint32_t hash_page (void *kpage) {
    uint32_t *s = (uint32_t *)kpage;
    uint32_t sum = 0;
    for (int i = 0; i < PGSIZE / sizeof (uint32_t); i ++) {
        sum = (sum * 2 + s[i] ) % 1000000007;
    }
    return sum;
}


/* Initialize parameters and frame table lock */
void init_frame_table (void) {

    init_frame_table_param (&frame_num, (uint32_t *)&base);
    printf("%d frames available in user kernel, start at %p\n", frame_num, base);
    clock_hand = 0;
    lock_init (&frame_lock);

    int page_cnt = DIV_ROUND_UP (frame_num * sizeof (uint32_t *), PGSIZE );
    frame_table = (uint32_t **)palloc_get_multiple (PAL_ZERO, page_cnt);
    end_addr = base + frame_num * PGSIZE;
}

/* Retrive PTE from frame table given a frame address */
uint32_t *frame_to_pte (void *frame_addr) {
    ASSERT (pg_ofs (frame_addr) == 0); 
    ASSERT (frame_addr >= base && frame_addr < end_addr );

    uint32_t idx = (frame_addr - base) / PGSIZE;

    lock_acquire (&frame_lock);
    uint32_t *pte = frame_table[idx];
    lock_release (&frame_lock);
    
    return pte;
}
/*
    Implementation of the clock algorithm.
    1. If frame table is NULL, meaning an available frame, assign it.
    2. If frame is dirty, do operations, then unmark dirty, skip.
    3, If frame is accessed, unmark access flag, skip.
    4. Frame is considered idle, swap out content in the 
    linked page and return the page
    
    Note at this stage, frame table is not yet updated. */
void install_frame (uint32_t *target_pte) {

    ASSERT (target_pte && !pte_is_present (target_pte));
    void *uaddr = NULL;
    uint32_t *pte = NULL;
    bool flag = false;

    lock_acquire (&frame_lock);
    /* Scan for free frame */
    for (int i = 0; i < frame_num; i++) {
        pte = frame_table[clock_hand];
        if (pte == NULL) {
            flag = true;
            break;
        }
        clock_hand = (clock_hand + 1) % frame_num;
    }
    if (!flag) {
        while (1) {
            pte = frame_table[clock_hand];
            if (pte == pin_code_pte || pte == pin_stack_pte) {
                clock_hand = ( clock_hand + 1 ) % frame_num;
                continue;
            }
            if (pte_is_dirty (pte) ) {
                if (pte_is_mmapped (pte))
                    page_dirty_write (pte);
                pte_clear_dirty (pte);
                clock_hand = ( clock_hand + 1 ) % frame_num;
            } else if (pte_is_accessed (pte)) {
                pte_clear_accessed (pte);
                clock_hand = ( clock_hand + 1 ) % frame_num;
            } else if (!pte_is_swapped (pte)) {
                break;
            } else {
                printf("(%d)----old = %p , target = %p\n", thread_current()->tid, *pte, *target_pte);
                NOT_REACHED ();
            }
        }
        /* Store page in SWAP and uninstall frame from pte*/
        if (pte != NULL) {
            ASSERT ( !pte_is_swapped (pte) );
            ASSERT( store_in_swap (pte) );
            // printf("%d---store %p in swap\n", thread_current()->tid,pte);
        }
    }
// ----old = 0x3fd08f , target = 0x8e

    /* Install the new frame*/
    uaddr = base + clock_hand * PGSIZE;
    pte_install_frame (target_pte, uaddr);
    frame_table[clock_hand] = target_pte;
    // if (pte_is_swapped(target_pte)) {
    //     printf("----old (%p) = %p , target = %p\n", pte, *pte, *target_pte);
    //     PANIC("ass");
    // }
    clock_hand = ( clock_hand + 1 ) % frame_num;
    lock_release (&frame_lock);
    
    memset (uaddr, 0, PGSIZE);

}

void free_frame (uint32_t *pte) {
    ASSERT (pte && pte_is_present (pte));
    void *uaddr = pte_to_kpage (pte);
    uint32_t idx = (uaddr - base) / PGSIZE;

    lock_acquire (&frame_lock);
    frame_table[idx] = NULL;
    lock_release (&frame_lock);
}

void pin_frame (void *stack, void *code) {
    stack = pg_round_down (stack);
    code = pg_round_down (code);

    pin_stack_pte = uaddr_to_pte (stack);
    pin_code_pte =  uaddr_to_pte (code);
}


// pintos -v -k -T 60 --bochs  --filesys-size=2 -p tests/vm/page-parallel -a page-parallel -p tests/vm/child-linear -a child-linear --swap-size=4 -- -q  -f run page-parallel