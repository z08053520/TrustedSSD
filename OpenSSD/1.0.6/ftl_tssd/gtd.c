#include "gtd.h"
#include "mem_util.h"

#define GTD_ENTRY_ADDR(pmt_index)	(GTD_ADDR + sizeof(UINT32) * pmt_index)

void gtd_init(void)
{
	// TODO: load GTD from flash
	INFO("gtd>init", "# of GTD entries = %d, size of GTD = %dKB, # of GTD pages = %d",
				GTD_ENTRIES, GTD_SIZE / 1024, GTD_PAGES);

	mem_set_dram(GTD_ADDR, 0, GTD_BYTES);
}

void gtd_flush(void)
{
	// TODO: flush GTD to flash
}


UINT32 gtd_get_vpn(UINT32 const pmt_index)
{
	return read_dram_32(GTD_ENTRY_ADDR(pmt_index));
}

void   gtd_set_vpn(UINT32 const pmt_index, UINT32 const pmt_vpn)
{
	BUG_ON("set vpn=0 for pmt", pmt_vpn == 0);
	write_dram_32(GTD_ENTRY_ADDR(pmt_index), pmt_vpn);
}
