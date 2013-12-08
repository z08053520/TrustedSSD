#ifndef __GTD_H
#define __GTD_h
#include "pmt.h"

/* *
 * GTD = global translation table
 *
 *   Let's do some simple math to calculate the number of entries in GTD.
 * For a 64GB flash with 16KB page, GTD_ENTRIES = (64GB / 16KB) / (16KB / 4
 * bytes) = 1024. In other words, the size of GTD is 4KB. 
 * */
#define GTD_ENTRIES		PMT_PAGES
#define GTD_SIZE		(GTD_ENTRIES * sizeof(UINT32))
#define GTD_ENTRIES_PER_PAGE	(BYTES_PER_PAGE / sizeof(UINT32))	
#define GTD_PAGES		COUNT_BUCKETS(GTD_ENTRIES, GTD_ENTRIES_PER_PAGE)
#define GTD_BYTES		(BYTES_PER_PAGE * GTD_PAGES)
#define GTD_ADDR		(BAD_BLK_BMP_ADDR + BAD_BLK_BMP_BYTES)

void gtd_init(void);
void gtd_flush(void);

UINT32 gtd_get_vpn(UINT32 const pmt_index);
void   gtd_set_vpn(UINT32 const pmt_index, UINT32 const pmt_vpn);

#endif /* __GTD_H */
