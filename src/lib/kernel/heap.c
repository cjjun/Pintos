#include "heap.h"
#include "../debug.h"


void heap_init(struct heap *heap, heap_greater_func *greater, void *aux){

    ASSERT(heap != NULL);
    ASSERT(greater != NULL);

    heap->root = NULL;
    heap->greater = greater;
    heap->aux = aux;
    heap->size = 0;
}


void heap_elem_swap(struct heap_elem *a, struct heap_elem *b){
    ASSERT(a && b);
    struct heap_elem tmp = *a;
    *a = *b;
    *b = tmp;

    // If a and b are connected
    if(a->pa && a->pa == a)
        a->pa = b;
    if(b->pa && b->pa == b)
        b->pa = a;

    // revise parent's child record
    if(b->pa){
        if(a == b->pa->lc)
            b->pa->lc = b;
        else
            b->pa->rc = b;
    } 

    if(a->pa){
        if(b == a->pa->lc)
            a->pa->lc = a;
        else
            a->pa->rc = a;
    } 
    // revise children's parent
    if(a->lc)
        a->lc->pa = a;
    if(a->rc)
        a->rc->pa = a;
    
    if(b->lc)
        b->lc->pa = b;
    if(b->rc)
        b->rc->pa = b;
}

struct heap_elem *heap_top (struct heap *heap){
    return heap->root;
}

void heapify (struct heap *heap){
    ASSERT(heap != NULL);

    struct heap_elem *root = heap->root;
    heap_greater_func *greater = heap->greater;
    void *aux = heap->aux;

    while(root->lc || root->rc){
        struct heap_elem *max_elem = root;
        if(root->lc && greater(root->lc, max_elem, aux) )
            max_elem = root->lc;
        if(root->rc && greater(root->rc, max_elem, aux) )
            max_elem = root->rc;
        
        if(max_elem == root)
            return;
        heap_elem_swap(root, max_elem);
        if (heap->root == root)
                heap->root = max_elem;
    }

}

void heap_push_up (struct heap *heap, struct heap_elem *elem){

    ASSERT(heap != NULL);
    ASSERT(elem != NULL);

    if(elem->pa)
        ASSERT(elem->pa->lc==elem || elem->pa->rc == elem);
    // struct heap_elem *root = heap->root;
    heap_greater_func *greater = heap->greater;
    void *aux = heap->aux;

    while(elem->pa && greater(elem, elem->pa, aux))
        heap_elem_swap(elem, elem->pa);

    if(elem->pa == NULL)
        heap->root = elem;
}

struct heap_elem *heap_indexing (struct heap *heap, int idx){
    ASSERT(heap != NULL);
    ASSERT(idx <= heap->size);
    
    if(heap->size == 0)
        return NULL;
    if(idx == 1)
        return heap->root;
    struct heap_elem *pa = heap_indexing(heap, idx / 2);
    if(idx & 1)
        return pa->rc;
    else 
        return pa->lc;
}

void heap_push (struct heap *heap, struct heap_elem *elem){
    ASSERT(heap != NULL);
    ASSERT(elem != NULL);
    
    if(heap->root == NULL){
        heap->root = elem;
        elem->pa = elem->lc = elem->rc = NULL;
    }
    else{
        struct heap_elem *pa = heap_indexing( heap, (heap->size + 1) / 2 );
        if(pa->lc == NULL)
            pa->lc = elem;
        else 
            pa->rc = elem;
        elem->pa = pa;
        elem->lc = elem->rc = NULL;
        
    }
    heap->size++;
    heap_push_up(heap, elem);
}

void heap_adjust (struct heap *heap, struct heap_elem *elem){
    assert(heap && elem);
    if(elem->pa)
        assert(elem == elem->pa->lc || elem == elem->pa->rc);
    else 
        assert(elem == heap->root);

    if(heap->root == elem){
        heapify(heap);
        return;
    }
    else{
        heap_push_up(heap, elem);

        heap_greater_func *greater = heap->greater;
        void *aux = heap->aux;

        while(elem->lc || elem->rc){
            struct heap_elem *max_elem = elem;
            if(elem->lc && greater(elem->lc, max_elem, aux) )
                max_elem = elem->lc;
            if(elem->rc && greater(elem->rc, max_elem, aux) )
                max_elem = elem->rc;
            
            if(max_elem == elem)
                return;
            heap_elem_swap(elem, max_elem);
            assert(heap->root != elem);
        }
    }
}

struct heap_elem *heap_pop (struct heap *heap){
    ASSERT(heap != NULL);
    if(heap->root == NULL)
        return NULL;

    struct heap_elem *last = heap_indexing(heap, heap->size), *root = heap->root;
    if(last == root){
        heap->root = NULL; 
        heap->size--;
        return last;
    }

    heap_elem_swap(last, root);
    if(root->pa){
        if(root == root->pa->lc)
            root->pa->lc = NULL;
        else 
            root->pa->rc = NULL;
    }
    root->pa = root->lc = root->rc = NULL;

    heap->root = last;
    heap->size--;
    
    heapify(heap);
    return root;
    
}

int heap_size (struct heap * heap){
    return heap->size;
}

bool heap_empty(struct heap *heap){
    return heap->size == 0;
}

