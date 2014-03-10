// Copyright 2011 INDILINX Co., Ltd.
//
// This file is part of Jasmine.
//
// Jasmine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Jasmine is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Jasmine. See the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.


#ifndef	JASMINE_H
#define	JASMINE_H

#define	FLASH_TYPE		K9LCG08U1M
#define	DRAM_SIZE		65075200
// Default flash modules configuration
//#define	BANK_BMP		0x00330033
#define	BANK_BMP		0x00FF00FF
#define	CLOCK_SPEED		175000000

#define OPTION_ENABLE_ASSERT		1	// 1 = enable ASSERT() for debugging, 0 = disable ASSERT()
#define OPTION_UART_DEBUG		1	// 1 = enable UART message output, 0 = disable
#define OPTION_SLOW_SATA		0	// 1 = SATA 1.5Gbps, 0 = 3Gbps
#define OPTION_SUPPORT_NCQ		0	// 1 = support SATA NCQ (=FPDMA) for AHCI hosts, 0 = support only DMA mode
#define OPTION_REDUCED_CAPACITY		0	// reduce the number of blocks per bank for testing purpose

#define OPTION_PERF_TUNING		1

/* About macro OPTION_FTL_TEST
 *
 * This macro can be defined in Makefile. If it is defined, then the firmware
 * is running in FTL test mode without SATA communication; otherwise it is in
 * normal mode.
 *
 * To run unit tests, you have to specify which test case to run in the
 * Makefile, which will automatically define OPTION_FTL_TEST.
 * */

/* About macro OPTION_PROFILING
 *
 * If this marco is defined, profiling will be enabled to analyze time spent
 * by major compoents/functions. This macro is defined in Makefile.
 * */

/* About macro OPTION_FDE
 *
 * If this marco is defined, full disk encryption (FDE) will be enabled.
 * This macro is defined in Makefile.
 * */

/* About macro OPTION_2_PLANE
 *
 * Flash performance profiling result shows that flash throughput is
 * remarkably larger when the page size is 32KB. Thus, it is better to enable
 * OPTION_2_PLANE. TSSD FTL can work with a page size of either 16KB or 32KB.
 * */
#define	OPTION_2_PLANE			1	// 1 = 2-plane mode, 0 = 1-plane mode

/* About macro OPTION_ACL
 *
 * Access Control Layer (ACL) implements the core feature-- fine-grained
 * access control--- of TrustedSSD, which makes the latter unique to all SSD.
 * Use macro OPTION_ACL to enable ACL.
 * */
#define OPTION_ACL			0

#define CHN_WIDTH			2 	// 2 = 16bit IO
#define NUM_CHNLS_MAX		4
#define BANKS_PER_CHN_MAX	8
#define	NUM_BANKS_MAX		32

#include "nand.h"


/////////////////////////////////
// size	constants
/////////////////////////////////

#define	BYTES_PER_SECTOR		512

#define NUM_PSECTORS_8GB		16777216
#define NUM_PSECTORS_16GB		(NUM_PSECTORS_8GB*2)
#define NUM_PSECTORS_32GB		(NUM_PSECTORS_16GB*2)
#define NUM_PSECTORS_40GB		(NUM_PSECTORS_8GB*5)
#define NUM_PSECTORS_48GB		(NUM_PSECTORS_16GB*3)
#define	NUM_PSECTORS_64GB		(NUM_PSECTORS_32GB*2)
#define	NUM_PSECTORS_80GB		(NUM_PSECTORS_16GB*5)
#define	NUM_PSECTORS_96GB		(NUM_PSECTORS_32GB*3)
#define	NUM_PSECTORS_128GB		(NUM_PSECTORS_64GB*2)
#define	NUM_PSECTORS_160GB		(NUM_PSECTORS_32GB*5)
#define	NUM_PSECTORS_192GB		(NUM_PSECTORS_64GB*3)
#define	NUM_PSECTORS_256GB		(NUM_PSECTORS_128GB*2)
#define	NUM_PSECTORS_320GB		(NUM_PSECTORS_64GB*5)
#define	NUM_PSECTORS_384GB		(NUM_PSECTORS_128GB*3)
#define	NUM_PSECTORS_512GB		(NUM_PSECTORS_256GB*2)

#define	BYTES_PER_PAGE			(BYTES_PER_SECTOR *	SECTORS_PER_PAGE)
#define	BYTES_PER_PAGE_EXT		((BYTES_PER_PHYPAGE + SPARE_PER_PHYPAGE) * PHYPAGES_PER_PAGE)
#define BYTES_PER_PHYPAGE		(BYTES_PER_SECTOR *	SECTORS_PER_PHYPAGE)
#define	BYTES_PER_VBLK			(BYTES_PER_SECTOR *	SECTORS_PER_VBLK)
#define BYTES_PER_BANK			((UINT64) BYTES_PER_PAGE * PAGES_PER_BANK)
#define BYTES_PER_SMALL_PAGE	(BYTES_PER_PHYPAGE * CHN_WIDTH)
#if OPTION_2_PLANE
#define PHYPAGES_PER_PAGE		(CHN_WIDTH * NUM_PLANES)
#else
#define PHYPAGES_PER_PAGE		CHN_WIDTH
#endif
#define	SECTORS_PER_PAGE		(SECTORS_PER_PHYPAGE * PHYPAGES_PER_PAGE)
#define SECTORS_PER_SMALL_PAGE	(SECTORS_PER_PHYPAGE * CHN_WIDTH)
#define	SECTORS_PER_VBLK		(SECTORS_PER_PAGE *	PAGES_PER_VBLK)
#define SECTORS_PER_BANK		(SECTORS_PER_PAGE * PAGES_PER_BANK)
#define	PAGES_PER_BANK			(PAGES_PER_VBLK	* VBLKS_PER_BANK)
#define PAGES_PER_BLK           (PAGES_PER_VBLK)
#if OPTION_2_PLANE
#define VBLKS_PER_BANK			(PBLKS_PER_BANK / NUM_PLANES)
#define SPARE_VBLKS_PER_BANK	(SPARE_PBLKS_PER_BANK / NUM_PLANES)
#else
#define VBLKS_PER_BANK			PBLKS_PER_BANK
#define SPARE_VBLKS_PER_BANK	SPARE_PBLKS_PER_BANK
#endif

#define NUM_VBLKS				(VBLKS_PER_BANK * NUM_BANKS)
#define NUM_VPAGES				(PAGES_PER_VBLK * NUM_VBLKS)
#define NUM_PSECTORS			(SECTORS_PER_VBLK * ((VBLKS_PER_BANK - SPARE_VBLKS_PER_BANK) * NUM_BANKS))
#define	NUM_LPAGES				((NUM_LSECTORS + SECTORS_PER_PAGE - 1) / SECTORS_PER_PAGE)

#define ROWS_PER_PBLK			PAGES_PER_VBLK
#define ROWS_PER_BANK			(ROWS_PER_PBLK * PBLKS_PER_BANK)

/* Use 4KB sub page as the unit of page translation to improve the performance */
#define BYTES_PER_SUB_PAGE		4096
#define SUB_PAGES_PER_PAGE		(BYTES_PER_PAGE / BYTES_PER_SUB_PAGE)
#define SECTORS_PER_SUB_PAGE		(BYTES_PER_SUB_PAGE / BYTES_PER_SECTOR)

////////////////////
// block 0
////////////////////

#define BYTES_PER_FW_PAGE			2048
#define SECTORS_PER_FW_PAGE			(BYTES_PER_FW_PAGE / BYTES_PER_SECTOR)
#define SCAN_LIST_PAGE_OFFSET		0
#define STAMP_PAGE_OFFSET			4
#define	FW_PAGE_OFFSET				5


///////////////
// misc
///////////////

#define	FLASH_ID_BYTES			5

// 4 byte ECC parity is appended to the end of every 128 byte data
// The amount of DRAM space that you can use is reduced.
#define	DRAM_ECC_UNIT			128

#ifdef TRUE
#undef TRUE
#endif

#ifdef FALSE
#undef FALSE
#endif

#ifdef NULL
#undef NULL
#endif

#define	TRUE		1
#define	FALSE		0
#define	NULL		0
#define	OK			TRUE
#define	FAIL		FALSE
#define	INVALID8	((UINT8) -1)
#define	INVALID16	((UINT16) -1)
#define	INVALID32	((UINT32) -1)

typedef	unsigned char		BOOL8;
typedef	unsigned short		BOOL16;
typedef	unsigned int		BOOL32;
typedef	unsigned char		UINT8;
typedef	unsigned short		UINT16;
typedef	unsigned int		UINT32;
typedef unsigned long long	UINT64;

/* sector mask */
#if OPTION_2_PLANE
	typedef UINT64		sectors_mask_t;
	#define FULL_MASK	0xFFFFFFFFFFFFFFFFULL

	#define init_mask(offset, num_sectors)				\
			((num_sectors) >= sizeof(UINT64) * 8? 		\
				FULL_MASK :				\
				(((1ULL << (num_sectors)) - 1) << (offset)))
	#define count_sectors(mask)		__builtin_popcountll(mask)
	#define _begin_sector(mask)		__builtin_ctzll(mask)
	#define _end_sector(mask)		((sizeof(sectors_mask_t) * 8) - __builtin_clzll(mask))
#else
	typedef UINT32		sectors_mask_t;
	#define FULL_MASK	0xFFFFFFFFUL

	#define init_mask(offset, num_sectors)				\
			((num_sectors) >= sizeof(UINT32) * 8? 		\
				FULL_MASK :				\
				(((1UL << (num_sectors)) - 1) << (offset)))
	#define count_sectors(mask)		__builtin_popcount(mask)
	#define _begin_sector(mask)		__builtin_ctz(mask)
	#define _end_sector(mask)		((sizeof(sectors_mask_t) * 8) - __builtin_clz(mask))
#endif
// DEBUG
BOOL8 show_debug_msg;

UINT8 begin_sector(sectors_mask_t const mask);
UINT8 end_sector(sectors_mask_t const mask);

/* virtual page */
typedef union {
	struct {
		UINT32	bank  :5;
		UINT32	vpn   :27;
	};
	UINT32	as_uint;
} vp_t;

#define vp_equal(vp0, vp1)		((vp0).as_uint == (vp1).as_uint)
#define vp_not_equal(vp0, vp1)		((vp0).as_uint != (vp1).as_uint)

/* virtual sub-page */
typedef union {
	struct {
		UINT32	bank  :5;
		UINT32	vspn  :27;
	};
	UINT32	as_uint;
} vsp_t;

#define vsp_is_equal(vsp0, vsp1)	((vsp0).as_uint == (vsp1).as_uint)
#define vsp_not_equal(vsp0, vsp1)	((vsp0).as_uint != (vsp1).as_uint)

#define NULL_LPN			0xFFFFFFFF

#define COUNT_BUCKETS(TOTAL, BUCKET_SIZE) \
	( ((TOTAL) + (BUCKET_SIZE) - 1) / (BUCKET_SIZE) )

#define MIN(X, Y)				((X) > (Y) ? (Y) : (X))
#define MAX(X, Y)				((X) > (Y) ? (X) : (Y))

#define align_to(num, align)	((num) / (align) * (align))

void delay(UINT32 const count);

typedef UINT16		user_id_t;

#include "flash.h"
#include "sata.h"
#include "sata_cmd.h"
#include "sata_registers.h"
#include "mem_util.h"
#include "target.h"
#include "bank.h"

//////////////
// scan list
//////////////

#define SCAN_LIST_SIZE				BYTES_PER_SMALL_PAGE
#define SCAN_LIST_ITEMS				((SCAN_LIST_SIZE / sizeof(UINT16)) - 1)

typedef struct
{
	UINT16	num_entries;
	UINT16	list[SCAN_LIST_ITEMS];
}
scan_list_t;

#define NUM_LSECTORS	(21168 + ((NUM_PSECTORS) / 2097152 * 1953504)) // 125045424 ~= 59GiB

#include "misc.h"

#ifndef PROGRAM_INSTALLER
#include "uart.h"
#endif

#define FOR_EACH_BANK(i)	for(i=0;i<NUM_BANKS;i++)

// page-level striping technique (I/O parallelism)
#define lpn2bank(lpn)             ((lpn) % NUM_BANKS)

#if OPTION_UART_DEBUG
	/* log levels */
	#define LL_INFO 	0
	#define LL_DEBUG	1
	#define LL_WARNING 	2
	#define LL_ERROR	3
	/* only logs with level no less than LL_LEVEL are printed */
	#define LL_LEVEL 	LL_DEBUG

	#define __BUG_REPORT(_cond, _format, _args ...)\
		uart_printf("%s:%d: error in function '%s' for condition '%s': "\
			    _format "\r\n", __FILE__, __LINE__, __FUNCTION__,\
			    #_cond, ##_args)

	#define LOG(label, ...) do {\
		uart_printf("[%s] ", label);\
		uart_printf(__VA_ARGS__);\
		uart_printf(" <function %s, line %d, file %s>\r\n", __FUNCTION__, __LINE__, __FILE__);\
	} while(0);

	#if LL_INFO >= LL_LEVEL
		#define INFO(label, ...) 	LOG(label, __VA_ARGS__)
	#else
		#define INFO(label, ...)
	#endif

	#if LL_DEBUG >= LL_LEVEL
		#define DEBUG(label, ...) 	LOG(label, __VA_ARGS__)
	#else
		#define DEBUG(label, ...)
	#endif

	#if LL_WARNING >= LL_LEVEL
		#define WARNING(label, ...)	LOG(label, __VA_ARGS__);
	#else
		#define WARNING(label, ...)
	#endif

	#if LL_ERROR >= LL_LEVEL
		#define ERROR(label, ...)	LOG(label, __VA_ARGS__);
	#else
		#define ERROR(label, ...)
	#endif

	/* #define BUG_ON(MESSAGE, COND) do {\ */
	/* 	if (COND) {\ */
	/* 		__BUG_REPORT(COND, MESSAGE);\ */
	/* 		led_blink();\ */
	/* 		while(1);\ */
	/* 	}\ */
	/* } while(0); */
	#define BUG_ON(MESSAGE, COND) do {\
		if (COND) {\
			uart_print("bug on at line %u in file %s", __LINE__, __FILE__);\
			while(1);\
		}\
	} while(0);

#else
	#define LOG(label, ...)
	#define DEBUG(label, ...)
	#define INFO(label, ...)
	#define WARNING(label, ...)
	#define ERROR(label, ...)
	#define BUG_ON(MESSAGE, COND)
#endif

#define unlikely(x)	(__builtin_expect(!!(x), 0))

/* Simulate lamba by using the nested function feature of GCC */
#define lambda(return_type, ...)			\
		__extension__				\
		({					\
			return_type __fn__ __VA_ARGS__	\
			__fn__;				\
		})

#endif	// JASMINE_H

