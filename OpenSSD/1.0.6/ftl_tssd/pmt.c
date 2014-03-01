#include "pmt.h"
#include "page_cache.h"

/* ========================================================================= *
 * Public API
 * ========================================================================= */

void pmt_init(void)
{
	INFO("pmt>init", "# of PMT entries = %d, # of PMT pages = %d",
			PMT_ENTRIES, PMT_PAGES);
}

task_res_t pmt_load(UINT32 const lpn)
{
	UINT32 	pmt_idx  = pmt_get_index(lpn);
	return page_cache_load(pmt_idx);
}

void pmt_get_vp(UINT32 const lpn, UINT8 const sp_offset, vp_t *vp)
{
	UINT32	pmt_buff;
	UINT32	pmt_idx  = pmt_get_index(lpn);
	page_cache_get(pmt_idx, &pmt_buff, FALSE);
	BUG_ON("buffer is empty", pmt_buff == NULL);

	UINT32	pmt_offset = pmt_get_offset(lpn) * sizeof(pmt_entry_t)
				+ (UINT32)(&((pmt_entry_t*)0)->vps[sp_offset]);
	vp->as_uint = read_dram_32(pmt_buff + pmt_offset);
}

void pmt_update_vp(UINT32 const lpn, UINT8 const sp_offset, vp_t const vp)
{
	UINT32	pmt_buff;
	UINT32	pmt_idx  = pmt_get_index(lpn);
	page_cache_get(pmt_idx, &pmt_buff, TRUE);
	BUG_ON("buffer is empty", pmt_buff == NULL);

	UINT32	pmt_offset = pmt_get_offset(lpn) * sizeof(pmt_entry_t)
				+ (UINT32)(&((pmt_entry_t*)0)->vps[sp_offset]);
	write_dram_32(pmt_buff + pmt_offset, vp.as_uint);
}
