#ifndef __TEST_UTIL_H
#define __TEST_UTIL_H

#include "jasmine.h"

BOOL8 is_buff_wrong(UINT32 buff_addr, UINT32 val,
			   UINT8 offset, UINT8 num_sectors)
{
	if (offset >= SECTORS_PER_PAGE || num_sectors == 0) return FALSE;	
/*  	
	UINT32 i;
	UINT32 v;
	UINT32 j;

	INFO("is_buff_wrong", "Print the first/last five UINT32 values in every valid sectors of buffer: from %d, to %d",
				offset, offset + num_sectors);
	for(j = 0; j < num_sectors; j++) {
		uart_printf("sector %d: ", offset + j);
		for(i = 0; i < 5; i++) {
			v = read_dram_32(buff_addr + BYTES_PER_SECTOR * (offset+j) + i * sizeof(UINT32));
			if (i) uart_printf(", ");
			uart_printf("%d", v);
		}
		uart_printf("... ");
		for(i = 5; i > 0; i--) {
			v = read_dram_32(buff_addr + BYTES_PER_SECTOR * (offset+j+1) - i * sizeof(UINT32));
			if (i != 5) uart_printf(", ");
			uart_printf("%d", v);
		}
		uart_print("");
	}
*/	
	buff_addr    	    = buff_addr + BYTES_PER_SECTOR * offset;
	UINT32 buff_entries = BYTES_PER_SECTOR * num_sectors / sizeof(UINT32);

    	UINT32 min_idx  = mem_search_min_max(
				buff_addr,    sizeof(UINT32), 
				buff_entries, MU_CMD_SEARCH_MIN_DRAM);
	UINT32 min_val  = read_dram_32(buff_addr + min_idx * sizeof(UINT32));
	if (min_val != val) {
		uart_printf("expect min val to be %d but was %d, at position %d\r\n", val, min_val, min_idx);
		return TRUE;
	}
    	
	UINT32 max_idx  = mem_search_min_max(
				buff_addr,    sizeof(UINT32),
                              	buff_entries, MU_CMD_SEARCH_MAX_DRAM);
	UINT32 max_val  = read_dram_32(buff_addr + max_idx * sizeof(UINT32));
	if (max_val != val) {
		uart_printf("expect max val to be %d but was %d, at position %d\r\n", val, min_val);
		return TRUE;
	}

	return FALSE;
}

#endif
