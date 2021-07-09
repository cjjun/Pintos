#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>

uint32_t *pagedir_create (void);
uint32_t * pagedir_set_virtual_page (uint32_t *pd, void *upage, bool writable);

void pagedir_destroy (uint32_t *pd);
bool pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw);
void *pagedir_get_page (uint32_t *pd, const void *upage);
void pagedir_clear_page (uint32_t *pd, void *upage);
bool pagedir_is_dirty (uint32_t *pd, const void *upage);
void pagedir_set_dirty (uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed (uint32_t *pd, const void *upage);
bool pagedir_is_valid (uint32_t *pd, const void *vpage); 
void pagedir_set_accessed (uint32_t *pd, const void *upage, bool accessed);
void pagedir_activate (uint32_t *pd);

void pte_set_swap (uint32_t *pte, bool swap);
void pte_set_mmap (uint32_t *pte, bool mmap);
void pte_clear_accessed (uint32_t *pte);
void pte_clear_dirty (uint32_t *pte);

bool pte_is_swapped (uint32_t *pte);
bool pte_is_mmapped (uint32_t *pte);
bool pte_is_accessed (uint32_t *pte);
bool pte_is_dirty (uint32_t *pte);
bool pte_is_present (uint32_t *pte);
bool pte_is_valid (uint32_t *pte);

void pte_install_frame (uint32_t *pte, void *kpage);
void pte_uninstall_frame (uint32_t *pte);

uint32_t *lookup_pte (void *pd, void *uaddr);

#endif /* userprog/pagedir.h */
