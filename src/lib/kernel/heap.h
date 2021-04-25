#ifndef __LIB_KERNEL_HEAP_H
#define __LIB_KERNEL_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct heap_elem{
    struct heap_elem *pa, *lc, *rc;
};

typedef bool heap_greater_func (const struct heap_elem *a,
                             const struct heap_elem *b,
                             void *aux);
                             
struct heap{
    struct heap_elem *root;
    heap_greater_func *greater;
    int size;
    void *aux;
};


#define heap_entry(HEAP_ELEM, STRUCT, MEMBER) ( (STRUCT *) ((unsigned char *)HEAP_ELEM - offsetof (STRUCT, MEMBER)) )

void heap_init(struct heap *, heap_greater_func *, void *);
// void heap_push_up (struct heap *, struct heap_elem *);
void heap_push (struct heap *, struct heap_elem *);

struct heap_elem *heap_pop (struct heap *);
struct heap_elem *heap_top (struct heap *);

int heap_size (struct heap *);
bool heap_empty(struct heap *);
// struct heap_elem *heap_indexing (struct heap *, int);
void heap_adjust (struct heap *, struct heap_elem *);

#endif