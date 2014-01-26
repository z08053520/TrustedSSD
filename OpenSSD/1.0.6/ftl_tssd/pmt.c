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
	UINT32	lspn = lpn * SUB_PAGES_PER_PAGE;
	UINT32 	pmt_index  = pmt_get_index(lspn);
	pc_key_t key	   = {.type = PC_TYPE_PMT, .idx = pmt_index};
	return page_cache_load(key);
}

void pmt_get(UINT32 const lspn, vp_t *vp)
{
	UINT32	pmt_buff;
	UINT32	pmt_index  = pmt_get_index(lspn);
	UINT32	pmt_offset = pmt_get_offset(lspn);
	pc_key_t pmt_key = {.type = PC_TYPE_PMT, .idx = pmt_index};
	page_cache_get(pmt_key, &pmt_buff, FALSE);
	BUG_ON("buffer is empty", pmt_buff == NULL);

	vp_or_int res = {
		.as_int = read_dram_32(pmt_buff + sizeof(UINT32) * pmt_offset)
	};
	*vp = res.as_vp;
}

void pmt_update(UINT32 const lspn, vp_t const vp)
{
	UINT32 pmt_buff;
	UINT32 pmt_index  = pmt_get_index(lspn);
	UINT32 pmt_offset = pmt_get_offset(lspn);
	pc_key_t pmt_key = {.type = PC_TYPE_PMT, .idx = pmt_index};
	page_cache_get(pmt_key, &pmt_buff, TRUE);
	BUG_ON("buffer is empty", pmt_buff == NULL);

	vp_or_int val = {.as_vp = vp};
	write_dram_32(pmt_buff + sizeof(UINT32) * pmt_offset, val.as_int);
}
