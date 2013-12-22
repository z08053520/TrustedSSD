#ifndef __BAD_BLOCKS_H
#define __BAD_BLOCKS_H
#include "jasmine.h"

void bb_init();

BOOL32 bb_is_bad(UINT32 const bank, UINT32 const blk);

UINT32 bb_get_num(UINT32 const bank);

#endif /* __BAD_BLOCKS_H */
