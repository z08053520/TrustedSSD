#include "pmt.h"
#include "cache.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

#define pmt_get_index(lpn)	lpn / PMT_ENTRIES_PER_PAGE
#define pmt_get_offset(lpn)	lpn % PMT_ENTRIES_PER_PAGE

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static UINT32 load_pmt_buffer(UINT32 const pmt_index)
{
	UINT32 pmt_buff;

	cache_get(pmt_index, &pmt_buff, CACHE_BUF_TYPE_PMT);
	if (pmt_buff == NULL) {
		INFO("pmt>load pmt buffer", "cache miss for pmt idx = %d", pmt_index);
		cache_put(pmt_index, &pmt_buff, CACHE_BUF_TYPE_PMT);
		cache_fill_full_page(pmt_index, CACHE_BUF_TYPE_PMT);
	}
	else
		INFO("pmt>load pmt buffer", "cache hit for pmt idx = %d", pmt_index);
	return pmt_buff;
}

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
	UINT32 pmt_buff = load_pmt_buffer(pmt_index);	

	*vpn = read_dram_32(pmt_buff + sizeof(UINT32) * pmt_offset);
	INFO("pmt>fetch", "lpn = %d (vpn = %d, pmt_idx = %d)", lpn, *vpn, pmt_index);
}

void pmt_update(UINT32 const lpn, UINT32 const vpn)
{
	UINT32 pmt_index  = pmt_get_index(lpn);
	UINT32 pmt_offset = pmt_get_offset(lpn);
	UINT32 pmt_buff = load_pmt_buffer(pmt_index);

	write_dram_32(pmt_buff + sizeof(UINT32) * pmt_offset, vpn);
	INFO("pmt>update", "lpn = %d (new vpn = %d, pmt_idx = %d)", lpn, vpn, pmt_index);

	cache_set_dirty(pmt_index, CACHE_BUF_TYPE_PMT);
}
