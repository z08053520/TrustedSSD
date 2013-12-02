#ifndef __BAD_BLOCKS_H
#define __BAD_BLOCKS_H
#include "jasmine.h"

/* bitmap of bad blocks */
#define BAD_BLK_BMP_ADDR        (CACHE_ADDR + CACHE_BYTES)
#define BAD_BLK_BMP_BYTES	(COUNT_BUCKETS(NUM_VBLKS / 8, DRAM_ECC_UNIT) * DRAM_ECC_UNIT)

void bb_init();

BOOL32 bb_is_bad(UINT32 const bank, UINT32 const blk);

#endif /* __BAD_BLOCKS_H */
