#include "ftl_task.h"

UINT8	auto_idle_bank(banks_mask_t const idle_banks)
{
	static UINT8 bank_i = NUM_BANKS - 1;

	UINT8 i;
	for (i = 0; i < NUM_BANKS; i++) {
		bank_i = (bank_i + 1) % NUM_BANKS;
		if (banks_has(idle_banks, bank_i)) return bank_i;
	}
	return NUM_BANKS;
}
