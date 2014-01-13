#ifndef __FTL_TASK_H
#define __FTL_TASK_H

#include "jasmine.h"

#define lpn2lspn(lpn)		(lpn * SUB_PAGES_PER_PAGE)

#define begin_subpage(mask)	(begin_sector(mask) / SECTORS_PER_SUB_PAGE)
#define end_subpage(mask)	COUNT_BUCKETS(end_sector(mask), SECTORS_PER_SUB_PAGE)

#define mask_is_set(mask, i)		(((mask) >> (i)) & 1)
#define mask_set(mask, i)		((mask) |= (1 << (i)))
#define mask_clear(mask, i)		((mask) &= ~(1 << (i)))

#define banks_has(banks, bank)		mask_is_set(banks, bank)
#define banks_add(banks, bank)		mask_set(banks, bank)
#define banks_del(banks, bank)		mask_clear(banks, bank)

#define FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler, sp_mask)	\
	UINT8 i = 0;							\
	while (i < SECTORS_PER_SUB_PAGE) {				\
		/* find the first missing sector */			\
		while (i < SECTORS_PER_SUB_PAGE &&			\
		       mask_is_set(sp_mask, i)) i++;			\
		if (i == SECTORS_PER_SUB_PAGE) break;			\
		UINT8 begin_i = i++;					\
		/* find the last missing sector */			\
		while (i < SECTORS_PER_SUB_PAGE &&			\
		       !mask_is_set(sp_mask, i)) i++;			\
		UINT8 end_i   = i++;					\
		/* evoke segment handler */				\
		(*segment_handler)(begin_i, end_i);			\
	}

#endif
