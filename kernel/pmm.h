#ifndef _PMM_H_
#define _PMM_H_

#include "util/types.h"

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
#endif