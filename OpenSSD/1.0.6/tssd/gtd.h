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
#define GTD_ENTRIES_PER_PAGE	PMT_ENTRIES_PER_PAGE
#define GTD_PAGES		COUNT_BUCKETS(GTD_ENTRIES, GTD_ENTRIES_PER_PAGE)

UINT32 _GTD[GTD_ENTRIES];

#define gtd_get_vpn(pmt_index)		_GTD[pmt_index]
#define gtd_set_vpn(pmt_index, pmt_vpn)	_GTD[pmt_index] = pmt_vpn

void gtd_init(void);
void gtd_flush(void);

#endif /* __GTD_H */
