#ifndef __TEST_UTIL_H
#define __TEST_UTIL_H

#include "jasmine.h"
#if OPTION_FTL_TEST

/* ===========================================================================
 * Buffer Utility
 * =========================================================================*/
#define SETUP_BUF(name, addr, sectors)	\
		const UINT32 __BUF_##name##_BYTES = (BYTES_PER_SECTOR * (sectors));\
		static void init_##name##_buf(UINT32 const val) {\
			mem_set_dram((addr), val, __BUF_##name##_BYTES);\
		}\
		static UINT32 get_##name(UINT32 const i) {\
			UINT32 offset = sizeof(UINT32) * i;\
			BUG_ON("out of bound", offset >= __BUF_##name##_BYTES);\
			return read_dram_32((addr) + offset);\
		}\
		static void set_##name(UINT32 const i, UINT32 const val) {\
			UINT32 offset = sizeof(UINT32) * i;\
			BUG_ON("out of bound", offset >= __BUF_##name##_BYTES);\
			write_dram_32((addr) + offset, val);\
		}

/* ===========================================================================
 * Timer Utility
 * =========================================================================*/
void timer_reset();
UINT32 timer_ellapsed_us();

/* ===========================================================================
 * Performance Utility
 * =========================================================================*/
void perf_monitor_reset();
void perf_monitor_set_output_threshold(UINT32 const num_bytes);
void perf_monitor_report();
void perf_monitor_update(UINT32 const num_sectors);

/* ===========================================================================
 * Buffer Utility
 * =========================================================================*/
void clear_vals(UINT32 *sector_vals, UINT32 const val);
void set_vals(UINT32 *sector_vals, UINT32 const base_val,
	      UINT8  const offset, UINT8 const num_sectors);
void fill_buffer(UINT32 const buf, UINT8  const offset,
		 UINT8  const num_sectors, UINT32 *sectors_val);

/* ===========================================================================
 * Misc
 * =========================================================================*/
UINT32 random(UINT32 const min, UINT32 const max);

BOOL8 is_buff_wrong(UINT32 buff_addr, UINT32 val,
		    UINT8 offset, UINT8 num_sectors);

void  dump_buffer(UINT32 const buff_addr,
		  UINT8 const offset,
		  UINT8 const num_sectors);

#endif
#endif
