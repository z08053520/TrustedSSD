#ifndef __GC_H
#define __GC_H

#include "jasmine.h"

void gc_init(void);

UINT32 gc_allocate_new_vpn(UINT32 const bank, BOOL8 const is_sys);

UINT32 gc_get_num_free_blocks(UINT8 const bank);

#endif /* __GC__H */
