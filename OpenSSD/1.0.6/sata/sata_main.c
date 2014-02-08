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


#include "jasmine.h"
#include "dram.h"
#include "ftl.h"

sata_context_t		g_sata_context;
sata_ncq_t		g_sata_ncq;
volatile UINT32		g_sata_action_flags;

#define HW_EQ_SIZE		128
#define HW_EQ_MARGIN		4

#if OPTION_FTL_TEST

#define	CMD_QUEUE_SIZE		12
static CMD_T	queue[CMD_QUEUE_SIZE];
static UINT8	queue_size = 0;
static UINT8	queue_head = 0;
static UINT8	queue_tail = 0;

UINT32 eventq_get_count(void)
{
	return queue_size;
}

static void eventq_get(CMD_T* cmd)
{
	if (queue_size == 0) {
		cmd->sector_count = 0;
		return;
	}

	CMD_T *head_cmd = &queue[queue_head];
	cmd->lba 	  = head_cmd->lba;
	cmd->sector_count = head_cmd->sector_count;
	cmd->cmd_type 	  = head_cmd->cmd_type;

	queue_head = (queue_head + 1) % CMD_QUEUE_SIZE;
	queue_size--;
}

BOOL8 eventq_put(UINT32 const lba, UINT32 const sector_count, UINT32 const cmd_type)
{
	if (queue_size == CMD_QUEUE_SIZE) return TRUE;

	CMD_T *tail_cmd = &queue[queue_tail];
	tail_cmd->lba	= lba;
	tail_cmd->sector_count = sector_count;
	tail_cmd->cmd_type = cmd_type;

	queue_tail = (queue_tail + 1) % CMD_QUEUE_SIZE;
	queue_size++;
	return FALSE;
}

#else

static UINT32 eventq_get_count(void)
{
	return (GETREG(SATA_EQ_STATUS) >> 16) & 0xFF;
}

static void eventq_get(CMD_T* cmd)
{
	disable_fiq();

	SETREG(SATA_EQ_CTRL, 1);	// The next entry from the Event Queue is copied to SATA_EQ_DATA_0 through SATA_EQ_DATA_2.

	while ((GETREG(SATA_EQ_DATA_2) & 8) != 0);

	UINT32 EQReadData0	= GETREG(SATA_EQ_DATA_0);
	UINT32 EQReadData1	= GETREG(SATA_EQ_DATA_1);

	cmd->lba		= EQReadData1 & 0x3FFFFFFF;
	cmd->sector_count	= EQReadData0 >> 16;
	cmd->cmd_type		= EQReadData1 >> 31;
#if OPTION_ACL
	// FIXME: read real session_key from queue
	cmd->session_key	= cmd->lba;
#endif

	if(cmd->sector_count == 0)
		cmd->sector_count = 0x10000;

	if (g_sata_context.eq_full)
	{
		g_sata_context.eq_full = FALSE;

		if ((GETREG(SATA_PHY_STATUS) & 0xF0F) == 0x103)
		{
			SETREG(SATA_CTRL_2, g_sata_action_flags);
		}
	}

	enable_fiq();
}

#endif

__inline ATA_FUNCTION_T search_ata_function(UINT32 command_code)
{
	UINT32 index;
	ATA_FUNCTION_T ata_function;

	index = mem_search_equ(ata_command_code_table, sizeof(UINT8), CMD_TABLE_SIZE, MU_CMD_SEARCH_EQU_SRAM, command_code);

	ata_function = (index == CMD_TABLE_SIZE) ? ata_not_supported : ata_function_table[index];

	if (ata_function == (ATA_FUNCTION_T) INVALID32)
		ata_function = ata_not_supported;

	return ata_function;
}

BOOL8 sata_has_next_rw_cmd()
{
	return eventq_get_count() > 0;
}

void  sata_get_next_rw_cmd(CMD_T *cmd)
{
	return eventq_get(cmd);
}

static BOOL8 sata_has_slow_cmd()
{
	return g_sata_context.slow_cmd.status == SLOW_CMD_STATUS_PENDING;
}

static void  sata_handle_slow_cmd()
{
	void (*ata_function)(UINT32 lba, UINT32 sector_count);

	slow_cmd_t* slow_cmd = &g_sata_context.slow_cmd;
	slow_cmd->status = SLOW_CMD_STATUS_BUSY;

	ata_function = search_ata_function(slow_cmd->code);
	ata_function(slow_cmd->lba, slow_cmd->sector_count);

	slow_cmd->status = SLOW_CMD_STATUS_NONE;
}

void Main(void)
{
	while (1) {
		BOOL8 idle = ftl_main();

		if (idle && sata_has_slow_cmd())
			sata_handle_slow_cmd();
	}
}

void sata_reset(void)
{
	disable_interrupt();

	mem_set_sram(&g_sata_context, 0, sizeof(g_sata_context));

	g_sata_context.write_cache_enabled = TRUE;
	g_sata_context.read_look_ahead_enabled = TRUE;

	SETREG(PMU_ResetCon, RESET_SATA | RESET_SATADWCLK | RESET_SATAHCLK | RESET_PMCLK | RESET_PHYDOMAIN);
	delay(100);

	SETREG(PHY_DEBUG, 0x400A040E);
	while ((GETREG(PHY_DEBUG) & BIT30) == 1);

	SETREG(SATA_BUF_PAGE_SIZE, BYTES_PER_PAGE);
	SETREG(SATA_WBUF_BASE, (SATA_WR_BUF_ADDR - DRAM_BASE));
	SETREG(SATA_RBUF_BASE, (SATA_RD_BUF_ADDR - DRAM_BASE));
	SETREG(SATA_WBUF_SIZE, NUM_SATA_WR_BUFFERS);
	SETREG(SATA_RBUF_SIZE, NUM_SATA_RD_BUFFERS);
	SETREG(SATA_WBUF_MARGIN, 16);
	SETREG(SATA_RESET_WBUF_PTR, BIT0);
	SETREG(SATA_RESET_RBUF_PTR, BIT0);

	SETREG(SATA_NCQ_BASE, g_sata_ncq.queue);

	SETREG(SATA_EQ_CFG_1, BIT0 | BIT14 | BIT9 | BIT16 | ((NUM_BANKS / 2) << 24));
	SETREG(SATA_EQ_CFG_2, (EQ_MARGIN & 0xF) << 16);

	SETREG(SATA_CFG_10, BIT0);

	SETREG(SATA_NCQ_CTRL, AUTOINC | FLUSH_NCQ);
	SETREG(SATA_NCQ_CTRL, AUTOINC);
	SETREG(SATA_CFG_5, BIT12 | BIT11*BSO_RX_SSC | (BIT9|BIT10)*BSO_TX_SSC | BIT4*0x05);
	SETREG(SATA_CFG_8, 0);
	SETREG(SATA_CFG_9, BIT20);

	SETREG(SATA_MAX_LBA, MAX_LBA);

	SETREG(APB_INT_STS, INTR_SATA);

	#if OPTION_SLOW_SATA
	SETREG(SATA_PHY_CTRL, 0x00000310);
	#else
	SETREG(SATA_PHY_CTRL, 0x00000300);
	#endif

	SETREG(SATA_ERROR, 0xFFFFFFFF);
	SETREG(SATA_INT_STAT, 0xFFFFFFFF);

	SETREG(SATA_CTRL_1, BIT31);

	while ((GETREG(SATA_INT_STAT) & PHY_ONLINE) == 0);

	SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24);

	SETREG(SATA_INT_ENABLE, PHY_ONLINE);

	enable_interrupt();
}

void delay(UINT32 const count)
{
	static volatile UINT32 temp;
	UINT32 i;

	for (i = 0; i < count; i++)
	{
		temp = i;
	}
}

