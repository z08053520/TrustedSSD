#ifndef __TEST_UTIL_H
#define __TEST_UTIL_H

#include "jasmine.h"
#if OPTION_FTL_TEST

void timer_reset();
UINT32 timer_ellapsed_us();

void perf_monitor_reset();
void perf_monitor_set_output_threshold(UINT32 const num_bytes);
void perf_monitor_report();
void perf_monitor_update(UINT32 const num_sectors);

UINT32 random(UINT32 const min, UINT32 const max);

BOOL8 is_buff_wrong(UINT32 buff_addr, UINT32 val,
		    UINT8 offset, UINT8 num_sectors);

#endif
#endif
