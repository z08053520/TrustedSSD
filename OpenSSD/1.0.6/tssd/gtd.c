#include "gtd.h"

void gtd_init(void)
{
	// TODO: load GTD from flash
	mem_set_sram(_GTD, 0, GTD_ENTRIES * sizeof(UINT32));
}

void gtd_flush(void)
{
	// TODO: flush GTD to flash
}
