#ifndef __DRAM_H
#define __DRAM_H

/* This header file defines the DRAM userage for all components */

#include "bank.h"
#include "target.h"

/* ========================================================================= *
 * PC (PMT Cache)
 * ========================================================================= */

#define PC_ADDR			DRAM_BASE
#define PC_END			(PC_ADDR + PC_BYTES)
#define MIN_NUM_PC_BUFFERS	MAX_NUM_THREADS
#define NUM_PC_BUFFERS		MIN_NUM_PC_BUFFERS
/* #define NUM_PC_BUFFERS		64 */
#define NUM_PC_SUB_PAGES	(NUM_PC_BUFFERS * SUB_PAGES_PER_PAGE)
#define PC_BYTES		(NUM_PC_BUFFERS * BYTES_PER_PAGE)
#define PC_SUB_PAGE(i)		(PC_ADDR + BYTES_PER_SUB_PAGE * (i))

/* ========================================================================= *
 * Page Lock
 * ========================================================================= */

#define PL_ADDR			PC_END
#define PL_BYTES		BYTES_PER_PAGE
#define PL_END			(PL_ADDR + PL_BYTES)

/* ========================================================================= *
 * Bad Block
 * ========================================================================= */

/* bitmap of bad blocks */
#define BAD_BLK_BMP_ADDR        	PL_END
#define BAD_BLK_BMP_END			(BAD_BLK_BMP_ADDR + BAD_BLK_BMP_BYTES)
#define BAD_BLK_BMP_BYTES_PER_BANK	COUNT_BUCKETS(VBLKS_PER_BANK, 8)
#define BAD_BLK_BMP_REAL_BYTES		(BAD_BLK_BMP_BYTES_PER_BANK * NUM_BANKS)
/*  #define BAD_BLK_BMP_BYTES		(COUNT_BUCKETS(\
						BAD_BLK_BMP_REAL_BYTES, \
						DRAM_ECC_UNIT) \
					  * DRAM_ECC_UNIT) */
/* TODO: make this smaller but not breaking SATA buffers address */
#define BAD_BLK_BMP_BYTES		(COUNT_BUCKETS(\
						BAD_BLK_BMP_REAL_BYTES, \
						BYTES_PER_PAGE) \
					  * BYTES_PER_PAGE)

/* ========================================================================= *
 * GTD
 * ========================================================================= */
#include "pmt.h"
#define GTD_ENTRIES		PMT_SUB_PAGES
#define GTD_NET_BYTES		(GTD_ENTRIES * sizeof(UINT32))
/* GTD must occupy whole pages */
#define GTD_PAGES		COUNT_BUCKETS(GTD_NET_BYTES, BYTES_PER_PAGE)
#define GTD_BYTES		(GTD_PAGES * BYTES_PER_PAGE)
#define GTD_ADDR		BAD_BLK_BMP_END
#define GTD_END			(GTD_ADDR + GTD_BYTES)

/* ========================================================================= *
 * Read and Write Buffers
 * ========================================================================= */

#define NUM_READ_BUFFERS	2
#define NUM_WRITE_BUFFERS	8

#define READ_BUF_ADDR		GTD_END
#define READ_BUF_BYTES		(NUM_READ_BUFFERS * BYTES_PER_PAGE)
#define READ_BUF_END		(READ_BUF_ADDR + READ_BUF_BYTES)
#define READ_BUF(i)		(READ_BUF_ADDR + BYTES_PER_PAGE * (i))

#define ALL_ONE_BUF		READ_BUF(0)
#define ALL_ZERO_BUF		READ_BUF(1)

/* Write buffer use managed buffer */

/* ========================================================================= *
 * ACL Table
 * ========================================================================= */

#if OPTION_ACL
#define ACL_TABLE_ADDR		READ_BUF_END
#define ACL_TABLE_ENTRIES	(NUM_BANKS * PAGES_PER_BANK)
#define ACL_TABLE_ENTRIES_PER_PAGE	\
				(BYTES_PER_PAGE / sizeof(user_id_t))
#define ACL_TABLE_NUM_PAGES	COUNT_BUCKETS(ACL_TABLE_ENTRIES,\
					ACL_TABLE_ENTRIES_PER_PAGE)
#define ACL_TABLE_BYTES		(ACL_TABLE_NUM_PAGES * BYTES_PER_PAGE)
#define ACL_TABLE_END		(ACL_TABLE_ADDR + ACL_TABLE_BYTES)

#define NON_BUFFER_AREA_END	ACL_TABLE_END
#else
#define NON_BUFFER_AREA_END	READ_BUF_END
#endif

/* ========================================================================= *
 * Other Non-SATA Buffers
 * ========================================================================= */

#define NUM_COPY_BUFFERS	NUM_BANKS_MAX
#define NUM_MANAGED_BUFFERS	(2 * NUM_BANKS + NUM_WRITE_BUFFERS)
#define NUM_HIL_BUFFERS		1
#define NUM_TEMP_BUFFERS	1
#define NUM_THREAD_SWAP_BUFFERS	1

#define COPY_BUF_ADDR          	NON_BUFFER_AREA_END
#define COPY_BUF_BYTES          (NUM_COPY_BUFFERS * BYTES_PER_PAGE)
#define _COPY_BUF(RBANK)	(COPY_BUF_ADDR + (RBANK) * BYTES_PER_PAGE)
#define COPY_BUF(BANK)		_COPY_BUF(REAL_BANK(BANK))

#define MANAGED_BUF_ADDR	(COPY_BUF_ADDR + COPY_BUF_BYTES)
#define MANAGED_BUF_BYTES       (NUM_MANAGED_BUFFERS * BYTES_PER_PAGE)
#define MANAGED_BUF(i)       	(MANAGED_BUF_ADDR + ((i) * BYTES_PER_PAGE))

#define HIL_BUF_ADDR            (MANAGED_BUF_ADDR + MANAGED_BUF_BYTES)
#define HIL_BUF_BYTES           (NUM_HIL_BUFFERS * BYTES_PER_PAGE)

#define TEMP_BUF_ADDR           (HIL_BUF_ADDR + HIL_BUF_BYTES)
#define TEMP_BUF_BYTES          (NUM_TEMP_BUFFERS * BYTES_PER_PAGE)

#define THREAD_SWAP_BUF_ADDR	(TEMP_BUF_ADDR + TEMP_BUF_BYTES)
#define THREAD_SWAP_BUF_BYTES	(NUM_THREAD_SWAP_BUFFERS * BYTES_PER_PAGE)

#define NON_SATA_BUF_END	(THREAD_SWAP_BUF_ADDR + THREAD_SWAP_BUF_BYTES)

/* ========================================================================= *
 * SATA Buffers
 * ========================================================================= */

#define NUM_NON_SATA_BUFFERS	(NUM_COPY_BUFFERS + NUM_MANAGED_BUFFERS + \
				 NUM_HIL_BUFFERS + NUM_TEMP_BUFFERS + \
				 NUM_THREAD_SWAP_BUFFERS + \
				 NUM_READ_BUFFERS)
#define NON_SATA_BUF_BYTES	(NUM_NON_SATA_BUFFERS * BYTES_PER_PAGE)
#define _DRAM_BYTES_OTHER	(NON_SATA_BUF_BYTES + \
				 PC_BYTES + PL_BYTES + \
				 BAD_BLK_BMP_BYTES + GTD_BYTES)
#if OPTION_ACL
#define DRAM_BYTES_OTHER	(_DRAM_BYTES_OTHER + ACL_TABLE_BYTES)
#else
#define DRAM_BYTES_OTHER	_DRAM_BYTES_OTHER
#endif

#define NUM_SATA_RW_BUFFERS	((DRAM_SIZE - DRAM_BYTES_OTHER) / BYTES_PER_PAGE - 1)
#define NUM_SATA_RD_BUFFERS	(COUNT_BUCKETS(NUM_SATA_RW_BUFFERS / 8, NUM_BANKS) * NUM_BANKS)
#define NUM_SATA_WR_BUFFERS	(NUM_SATA_RW_BUFFERS - NUM_SATA_RD_BUFFERS)

#define SATA_BUFS_ADDR		NON_SATA_BUF_END
#define SATA_RD_BUF_ADDR	SATA_BUFS_ADDR
#define SATA_RD_BUF_BYTES      	(NUM_SATA_RD_BUFFERS * BYTES_PER_PAGE)

#define SATA_WR_BUF_ADDR       	(SATA_RD_BUF_ADDR + SATA_RD_BUF_BYTES)
#define SATA_WR_BUF_BYTES      	(NUM_SATA_WR_BUFFERS * BYTES_PER_PAGE)

#define SATA_WR_BUF_PTR(BUF_ID)	(SATA_WR_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define SATA_WR_BUF_ID(BUF_PTR)	((((UINT32)BUF_PTR) - SATA_WR_BUF_ADDR) / BYTES_PER_PAGE)
#define SATA_RD_BUF_PTR(BUF_ID)	(SATA_RD_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define SATA_RD_BUF_ID(BUF_PTR)	((((UINT32)BUF_PTR) - SATA_RD_BUF_ADDR) / BYTES_PER_PAGE)

#endif
