#include "test_util.h"

#if OPTION_FTL_TEST
#include <stdlib.h>

/* ===========================================================================
 * Timer
 * =========================================================================*/

void timer_reset() 
{
    start_interval_measurement(TIMER_CH2, TIMER_PRESCALE_0);
}

UINT32 timer_ellapsed_us() 
{
    UINT32 rtime;

    rtime = 0xFFFFFFFF - GET_TIMER_VALUE(TIMER_CH2);
    // Tick to us
    rtime = (UINT32)((UINT64)rtime * 2 * 1000000 * PRESCALE_TO_DIV(TIMER_PRESCALE_0) / CLOCK_SPEED);
    return rtime;
}

/* ===========================================================================
 * Performance report 
 * =========================================================================*/

#define PM_DEFAULT_OUTPUT_THRESHOLD		(16 * 1024 * 1024) 	/* 16MB */

static UINT32 _pm_output_threshold = PM_DEFAULT_OUTPUT_THRESHOLD;
static UINT32 _pm_total_bytes;

void perf_monitor_reset() 
{
	_pm_total_bytes = 0;
	timer_reset();
}

void perf_monitor_set_output_threshold(UINT32 const num_bytes)
{
	_pm_output_threshold = num_bytes;
}

void perf_monitor_report()
{
	UINT32 time_us 	  = timer_ellapsed_us();
	UINT32 throughput = _pm_total_bytes / time_us;
  
	uart_printf("Transferred %d bytes (~%dMB) in %dus (~%dms), "
		    "throughput %dMB/s\r\n",
		    _pm_total_bytes, _pm_total_bytes / 1024 /1024,
		    time_us, time_us / 1000, 
		    throughput);
}

void perf_monitor_update(UINT32 const num_sectors)
{
	_pm_total_bytes += BYTES_PER_SECTOR * num_sectors;
	
	if (_pm_total_bytes >= _pm_output_threshold) {
		perf_monitor_report();
		perf_monitor_reset();
	}
}

/* ===========================================================================
 * Misc 
 * =========================================================================*/

UINT32 random(UINT32 const min, UINT32 const max)
{
	return min + (rand() % (max-min+1));
}

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
		uart_printf("expect min val to be %u but was %u, at position %u\r\n", val, min_val, min_idx);
		return TRUE;
	}
    	
	UINT32 max_idx  = mem_search_min_max(
				buff_addr,    sizeof(UINT32),
                              	buff_entries, MU_CMD_SEARCH_MAX_DRAM);
	UINT32 max_val  = read_dram_32(buff_addr + max_idx * sizeof(UINT32));
	if (max_val != val) {
		uart_printf("expect max val to be %u but was %u, at position %u\r\n", val, min_val);
		return TRUE;
	}

	return FALSE;
}
#endif
