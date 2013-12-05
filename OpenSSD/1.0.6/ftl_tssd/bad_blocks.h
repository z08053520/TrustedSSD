#ifndef __BAD_BLOCKS_H
#define __BAD_BLOCKS_H
#include "jasmine.h"

/* bitmap of bad blocks */
#define BAD_BLK_BMP_ADDR        	(BC_ADDR + BC_BYTES)
#define BAD_BLK_BMP_BYTES_PER_BANK	COUNT_BUCKETS(VBLKS_PER_BANK, 8)
#define BAD_BLK_BMP_REAL_BYTES		(BAD_BLK_BMP_BYTES_PER_BANK * NUM_BANKS)
#define BAD_BLK_BMP_BYTES		(COUNT_BUCKETS(\
						BAD_BLK_BMP_REAL_BYTES, \
						DRAM_ECC_UNIT) \
					  * DRAM_ECC_UNIT)

void bb_init();

BOOL32 bb_is_bad(UINT32 const bank, UINT32 const blk);

#endif /* __BAD_BLOCKS_H */
