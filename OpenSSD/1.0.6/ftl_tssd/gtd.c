#include "gtd.h"
#include "dram.h"
#include "mem_util.h"

static UINT32 gtd_zone_addr[NUM_GTD_ZONE_TYPES + 1] = {
#define ENTRY(zone_name)		(GTD_ADDR + ((UINT32) &((gtd_zone_t*)0)->zone_name)),
	ZONE_LIST
#undef ENTRY
	NULL
};

#define GTD_ENTRY_ADDR(idx, type)	(gtd_zone_addr[type] + sizeof(UINT32) * idx)

void gtd_init(void)
{
	// TODO: load GTD from flash
	INFO("gtd>init", "# of GTD entries = %d, size of GTD = %dKB, # of GTD pages = %d",
				GTD_SIZE / sizeof(UINT32), GTD_SIZE / 1024, GTD_PAGES);

	mem_set_dram(GTD_ADDR, 0, GTD_BYTES);
}

void gtd_flush(void)
{
	// TODO: flush GTD to flash
}

vsp_t gtd_get_vsp(UINT32 const index, gtd_zone_type_t const zone_type)
{
	vsp_or_int res;	
	res.as_int = read_dram_32(GTD_ENTRY_ADDR(index, zone_type));
	return res.as_vsp;
}

void   gtd_set_vsp(UINT32 const index, vsp_t const vsp, gtd_zone_type_t const zone_type)
{
	BUG_ON("set vspn in vpn #0 ", vsp.vspn < SUB_PAGES_PER_PAGE);
	vsp_or_int val = {.as_vsp = vsp};
	write_dram_32(GTD_ENTRY_ADDR(index, zone_type), val.as_int);
}
