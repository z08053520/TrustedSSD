#include "gtd.h"

void gtd_init(void)
{
	// TODO: load GTD from flash
	INFO("gtd>init", "# of GTD entries = %d, size of GTD = %dKB, # of GTD pages = %d",
				GTD_ENTRIES, GTD_SIZE / 1024, GTD_PAGES);
	mem_set_sram(_GTD, 0, GTD_ENTRIES * sizeof(UINT32));
}

void gtd_flush(void)
{
	// TODO: flush GTD to flash
}
