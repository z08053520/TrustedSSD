#include "gtd.h"
#include "dram.h"
#include "mem_util.h"

static UINT32 gtd_zone_addr[NUM_PAGE_TYPES + 1] = {
#define ENTRY(zone_name)	(GTD_ADDR + ((UINT32) &((gtd_zones_t*)0)->zone_name)),
	PAGE_TYPE_LIST
#undef ENTRY
	NULL
};

#define GTD_ENTRY_ADDR(key)	(gtd_zone_addr[(key).type] + sizeof(UINT32) * (key).idx)

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

vsp_t gtd_get_vsp(page_key_t const key)
{
	vsp_t vsp = {
		.as_uint = read_dram_32(GTD_ENTRY_ADDR(key))
	};
	return vsp;
}

void   gtd_set_vsp(page_key_t const key, vsp_t const vsp)
{
	BUG_ON("set vspn in vpn #0 ", vsp.vspn < SUB_PAGES_PER_PAGE);
	write_dram_32(GTD_ENTRY_ADDR(key), vsp.as_uint);
}
