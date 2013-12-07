#ifndef __GC_H
#define __GC_H

#include "jasmine.h"

void gc_init(void);

UINT32 gc_replace_old_vpn(UINT32 const bank, UINT32 const old_vpn);

UINT32 gc_allocate_new_vpn(UINT32 const bank);

UINT32 gc_get_num_free_pages(UINT32 const bank);

#endif /* __GC__H */
