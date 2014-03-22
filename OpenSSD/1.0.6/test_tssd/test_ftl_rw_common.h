#ifndef __TEST_FTL_RW_COMMON_H
#define __TEST_FTL_RW_COMMON_H

#include "dram.h"
#include "ftl.h"
#include "pmt_thread.h"
#include "scheduler.h"
#include "misc.h"
#include "test_util.h"
#include <stdlib.h>

#if OPTION_FTL_VERIFY != 1
	#error FTL verification is not enabled
#endif

/* #define DEBUG_FTL */
#ifdef DEBUG_FTL
	#define debug(format, ...)	uart_print(format, ##__VA_ARGS__)
#else
	#define debug(format, ...)
#endif

#if OPTION_ACL
extern BOOL8 eventq_put(UINT32 const lba, UINT32 const num_sectors,
			UINT32 const session_key, UINT32 const cmd_type);
#else
extern BOOL8 eventq_put(UINT32 const lba, UINT32 const num_sectors,
			UINT32 const cmd_type);
#endif
extern BOOL8 ftl_all_sata_cmd_accepted();

void finish_all()
{
	BOOL8 idle;
	do {
		idle = ftl_main();
	} while(!idle);
}

#define MAX_UINT32	0xFFFFFFFF
#define KB		1024
#define MB		(KB * KB)
#define GB		(MB * KB)
#define RAND_SEED	123456

#endif
