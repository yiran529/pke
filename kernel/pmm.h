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
#endif