#include "gtd.h"
#include "dram.h"
#include "mem_util.h"

#define GTD_ENTRY_ADDR(pmt_idx)		(GTD_ADDR + sizeof(vsp_t) * (pmt_idx))

void gtd_init(void)
{
	mem_set_dram(GTD_ADDR, 0, GTD_BYTES);
}

void gtd_flush(void)
{
	/* TODO: flush GTD to keep SSD state durable */
}

vsp_t gtd_get_vsp(UINT32 const pmt_idx)
{
	ASSERT(pmt_idx < PMT_SUB_PAGES);
	vsp_t vsp = {
		.as_uint = read_dram_32(GTD_ENTRY_ADDR(pmt_idx))
	};
	return vsp;
}

void   gtd_set_vsp(UINT32 const pmt_idx, vsp_t const vsp)
{
	ASSERT(pmt_idx < PMT_SUB_PAGES);
	ASSERT(vsp.vspn >= SUB_PAGES_PER_PAGE);
	write_dram_32(GTD_ENTRY_ADDR(pmt_idx), vsp.as_uint);
}
