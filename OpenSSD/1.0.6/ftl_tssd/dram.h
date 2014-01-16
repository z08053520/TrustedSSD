#ifndef __DRAM_H
#define __DRAM_H

/* This header file defines the DRAM userage for all components */

#include "bank.h"
#include "target.h"

/* ========================================================================= *
 * Page Cache  
 * ========================================================================= */

#define PC_ADDR			DRAM_BASE
#define PC_END			(PC_ADDR + PC_BYTES)
#define NUM_PC_BUFFERS		128	
//#define NUM_PC_BUFFERS		1
#define NUM_PC_SUB_PAGES	(NUM_PC_BUFFERS * SUB_PAGES_PER_PAGE)
#define PC_BYTES		(BYTES_PER_PAGE + NUM_PC_BUFFERS * BYTES_PER_PAGE)
#define PC_SUB_PAGE(i)		(PC_ADDR + BYTES_PER_PAGE + BYTES_PER_SUB_PAGE * (i))
#define PC_TEMP_BUF		PC_ADDR

/* ========================================================================= *
 * Buffer Cache  
 * ========================================================================= */

#define BC_ADDR			PC_END	
#define BC_END			(BC_ADDR + BC_BYTES)
//#define NUM_BC_BUFFERS_PER_BANK 16 
#define NUM_BC_BUFFERS_PER_BANK 1	
#define NUM_BC_BUFFERS		(NUM_BC_BUFFERS_PER_BANK * NUM_BANKS)
#define BC_BYTES		(NUM_BC_BUFFERS * BYTES_PER_PAGE)
#define BC_BUF(i)		(BC_ADDR + BYTES_PER_PAGE * (i))
#define BC_BUF_IDX(addr)	(((addr) - BC_ADDR) / BYTES_PER_PAGE)

/* ========================================================================= *
 * Bad Block 
 * ========================================================================= */

/* bitmap of bad blocks */
#define BAD_BLK_BMP_ADDR        	BC_END	
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
#include "gtd.h"
#define GTD_SIZE		sizeof(gtd_zone_t)
/* GTD must occupy whole pages */
#define GTD_BYTES		(COUNT_BUCKETS(GTD_SIZE, BYTES_PER_PAGE) * BYTES_PER_PAGE)
#define GTD_ADDR		BAD_BLK_BMP_END
#define GTD_END			(GTD_ADDR + GTD_BYTES)

/* ========================================================================= *
 * Read and Write Buffers
 * ========================================================================= */

#define NUM_READ_BUFFERS	1
#define NUM_WRITE_BUFFERS	8

#define READ_BUF_ADDR		GTD_END	
#define READ_BUF_BYTES		(NUM_READ_BUFFERS * BYTES_PER_PAGE)
#define READ_BUF_END		(READ_BUF_ADDR + READ_BUF_BYTES)

#define WRITE_BUF_ADDR		READ_BUF_END
#define WRITE_BUF_BYTES		(NUM_WRITE_BUFFERS * BYTES_PER_PAGE)
#define WRITE_BUF_END		(WRITE_BUF_ADDR + WRITE_BUF_BYTES)

#define READ_BUF(i)		(READ_BUF_ADDR + BYTES_PER_PAGE * (i))
#define WRITE_BUF(i)		(WRITE_BUF_ADDR + BYTES_PER_PAGE * (i))

/* ========================================================================= *
 * Other Non-SATA Buffers
 * ========================================================================= */

#define NUM_COPY_BUFFERS	NUM_BANKS_MAX
#define NUM_FTL_RD_BUFFERS	NUM_BANKS
#define NUM_FTL_WR_BUFFERS	NUM_BANKS
#define NUM_HIL_BUFFERS		1
#define NUM_TEMP_BUFFERS	1

#define COPY_BUF_ADDR          	WRITE_BUF_END 
#define COPY_BUF_BYTES          (NUM_COPY_BUFFERS * BYTES_PER_PAGE)

#define FTL_RD_BUF_ADDR		(COPY_BUF_ADDR + COPY_BUF_BYTES)
#define FTL_RD_BUF_BYTES       	(NUM_FTL_RD_BUFFERS * BYTES_PER_PAGE)

#define FTL_WR_BUF_ADDR         (FTL_RD_BUF_ADDR + FTL_RD_BUF_BYTES)
#define FTL_WR_BUF_BYTES        (NUM_FTL_WR_BUFFERS * BYTES_PER_PAGE)

#define HIL_BUF_ADDR            (FTL_WR_BUF_ADDR + FTL_WR_BUF_BYTES)
#define HIL_BUF_BYTES           (NUM_HIL_BUFFERS * BYTES_PER_PAGE)

#define TEMP_BUF_ADDR           (HIL_BUF_ADDR + HIL_BUF_BYTES)
#define TEMP_BUF_BYTES          (NUM_TEMP_BUFFERS * BYTES_PER_PAGE)

#define NON_SATA_BUF_END	(TEMP_BUF_ADDR + TEMP_BUF_BYTES)

#define _COPY_BUF(RBANK)	(COPY_BUF_ADDR + (RBANK) * BYTES_PER_PAGE)
#define COPY_BUF(BANK)		_COPY_BUF(REAL_BANK(BANK))
#define FTL_WR_BUF(BANK)       	(FTL_WR_BUF_ADDR + ((BANK) * BYTES_PER_PAGE))
#define FTL_RD_BUF(BANK)       	(FTL_RD_BUF_ADDR + ((BANK) * BYTES_PER_PAGE))

#define NUM_FTL_BUFFERS		(NUM_FTL_RD_BUFFERS + NUM_FTL_WR_BUFFERS)
#define	FTL_BUF_BYTES		(FTL_RD_BUF_BYTES   + FTL_WR_BUF_BYTES) 

/* ========================================================================= *
 * SATA Buffers
 * ========================================================================= */

#define NUM_NON_SATA_BUFFERS	(NUM_COPY_BUFFERS + NUM_FTL_BUFFERS + \
				 NUM_HIL_BUFFERS + NUM_TEMP_BUFFERS + \
				 NUM_READ_BUFFERS + NUM_WRITE_BUFFERS)
#define NON_SATA_BUF_BYTES	(NUM_NON_SATA_BUFFERS * BYTES_PER_PAGE)
#define DRAM_BYTES_OTHER	(NON_SATA_BUF_BYTES + PC_BYTES + BC_BYTES + BAD_BLK_BMP_BYTES + GTD_BYTES)

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
