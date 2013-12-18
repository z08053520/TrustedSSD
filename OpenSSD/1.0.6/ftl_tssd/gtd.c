#include "gtd.h"
#include "bad_blocks.h"
#include "buffer_cache.h"
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

UINT32 gtd_get_vpn(UINT32 const index, gtd_zone_type_t const zone_type)
{
	return read_dram_32(GTD_ENTRY_ADDR(index, zone_type));
}

void   gtd_set_vpn(UINT32 const index, UINT32 const vpn, gtd_zone_type_t const zone_type)
{
	BUG_ON("set vpn = 0 ", vpn == 0);
	write_dram_32(GTD_ENTRY_ADDR(index, zone_type), vpn);
}
