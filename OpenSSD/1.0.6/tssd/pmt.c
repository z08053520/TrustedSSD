#include "pmt.h"
#include "cache.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

#define pmt_get_index(lpn)	lpn / PMT_ENTRIES_PER_PAGE
#define pmt_get_offset(lpn)	lpn % PMT_ENTRIES_PER_PAGE

/* ========================================================================= *
 * Public API 
 * ========================================================================= */

void pmt_init(void)
{

}

void pmt_fetch(UINT32 const lpn, UINT32 *vpn)
{
	UINT32 pmt_index  = pmt_get_index(lpn);
	UINT32 pmt_offset = pmt_get_offset(lpn);
	UINT32 pmt_buff;

	cache_get(pmt_index, &pmt_buff, CACHE_BUF_TYPE_PMT);
	if (pmt_buff == NULL) {
		cache_put(pmt_index, &pmt_buff, CACHE_BUF_TYPE_PMT);
		cache_fill_full_page(pmt_index);
	}
	
	*vpn = read_dram_32(pmt_buff + sizeof(UINT32) * pmt_offset);
}

BOOL32 pmt_update(UINT32 const lpn, UINT32 const vpn)
{
//	UINT32 pmt_page_index = lpn % PMT_ENTRIES_PER_PAGE;
	return 0;	
}


