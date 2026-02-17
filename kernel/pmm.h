#ifndef _PMM_H_
#define _PMM_H_

#include "util/types.h"
#include "config.h"

// Initialize phisical memeory manager
void pmm_init();
// Allocate a free phisical page
void* alloc_page();
// Free an allocated page
void free_page(void* pa);

void inc_page_refcount(uint64 pa);
// inc/dec/get page refcount, all take a physical address (pa)
void inc_page_refcount(uint64 pa);
uint32 dec_page_refcount(uint64 pa); // return new count after decrement
uint32 get_page_refcount(uint64 pa);

// simple kernel bump allocator on pre-reserved contiguous physical memory
void *kmalloc(uint64 size);
// Save/restore heap position so debug data can be reclaimed when process exits.
uint64 kmalloc_mark(void);
void kmalloc_reset(uint64 mark);

// added @lab2_challenge3
extern int vm_alloc_stage[NCPU];
#endif