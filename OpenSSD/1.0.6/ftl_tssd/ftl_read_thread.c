#include "ftl_read_thread.h"
#include "thread_handler_util.h"
#include "write_buffer.h"
#include "pmt.h"
#include "fla.h"
#include "buffer.h"
#include "page_rw_lock.h"

/*
 * SATA
 * */

UINT32	g_num_ftl_read_tasks_submitted = 0;
UINT32 	g_num_ftl_read_tasks_finished = 0;
UINT32	g_next_finishing_task_seq_id = 0;

#define sata_rd_buf	(SATA_RD_BUF_PTR(var(seq_id) % NUM_SATA_RD_BUFFERS))

/*
 * Segment -- a segment is some sectors that that are stored in the same
 * virtual page
 * */
typedef struct {
	vp_t		vp;
	sectors_mask_t	target_sectors;
	BOOL8		is_issued:1;
	BOOL8		is_done:1;
	BOOL8		has_holes:1;
	UINT8		managed_buf_id;
} segment_t;

#define segment_init(seg, vp)	do {				\
		(seg)->vp = (vp);				\
		(seg)->target_sectors = 0;			\
		(seg)->is_issued = FALSE;			\
		(seg)->is_done = FALSE;				\
		(seg)->has_holes = FALSE;			\
		(seg)->managed_buf_id = NULL_BUF_ID;		\
	} while(0)

static BOOL8 check_segment_has_holes(segment_t *seg)
{
	sector_mask_t sectors = seg->target_sectors;
	ASSERT(sectors != 0);
	UINT8 middle_sectors = end_sector(sectors) - begin_sector(sectors);
	return count_sectors(sectors) < middle_sectors;
}

/*
 * Handler
 * */

begin_thread_stack
{
	UINT32		seq_id;
	UINT32		lpn;
	UINT8		sect_offset;
	UINT8		num_sectors;
	UINT8		num_segments;
	segment_t	segments[SUB_PAGES_PER_PAGE];
}
end_thread_stack

begin_thread_handler
{
/* Try write buffer first */
phase(BUFFER_PHASE) {
	if (page_read_lock(var(lpn))) run_later();

	sectors_mask_t	target_sectors = init_mask(
						var(sect_offset),
						var(num_sectors));

	UINT32 		wr_buf;
	sectors_mask_t	valid_sectors;
	write_buffer_get(var(lpn), &wr_buf, &valid_sectors);
	if (wr_buf == NULL) goto next_phase_mapping;

	sectors_mask_t	common_sectors = valid_sectors & target_sectors;
	if (common_sectors) {
		fla_copy_buffer(sata_rd_buf, wr_buf, common_sectors);
		target_sectors &= ~common_sectors;
	}
	if (target_sectors == 0) {
		page_unlock(var(lpn));
		goto_phase(SATA_PHASE);
	}
next_phase_mapping:
	var(target_sectors) = target_sectors;
}
/* Load PMT page and determine the segments */
phase(MAPPING_PHASE) {
	/* Make sure lpn-->vpn mapping is in PMT */
	if (!pmt_is_loaded(lpn)) {
		/* if can't load PMT, then we should wait for PMT to flush */
		if (pmt_load(lpn)) sleep(SIG_PMT_READY);
		/* wait for LPN to be loaded */
		else sleep(SIG_PMT_LOADED);
	}

	/* Iterate each sub-page to make segments */
	UINT8	num_segments = 0;
	segment_t *segments  = var(segments);
	UINT8	begin_sp  = begin_subpage(var(target_sectors)),
		end_sp	  = end_subpage(var(target_sectors));
	for (UINT8 sp_i = begin_sp; sp_i < end_sp; sp_i++) {
		sector_mask_t sp_sectors = init_mask(
						sp_i * SECTORS_PER_SUB_PAGE,
						SECTORS_PER_SUB_PAGE);
		sector_mask_t sp_target_sectors = var(target_sectors) & sp_sectors;
		if (sp_target_sectors == 0) continue;

		vp_t	vp;
		pmt_get_vp(var(lpn), sp_i, &vp);

		segment *seg; UINT8 seg_i;
		for (seg_i = 0; seg_i < num_segments; seg_i++) {
			seg = &segments[seg_i];
			if (vp_equal(seg->vp, vp)) break;
		}
		if (seg_i == num_segments) {
			seg = &segments[num_segments];
			segment_init(seg, vp);
			num_segments++;
		}
		seg->target_sectors |= sp_target_sectors;
	}

	var(num_segments) = num_segments;

	page_unlock(var(lpn));
}
/* Do flash read */
phase(FLASH_READ_PHASE) {
	while (1) {
		signals_t interesting_signals = 0;

		segment *seg; UINT8 bank, seg_i;
		for (seg_i = 0; seg_i < num_segments; seg_i++) {
			seg = & var(segments)[seg_i];

			if (seg->is_done) continue;
			bank = seg->vp.bank;

			/* check whether the issued flash cmd is complete */
			if (seg->is_issued) {
				/* check bank whether complete */
				if (!fla_is_bank_complete(bank)) {
					signals_set(interesting_signals,
							SIG_BANK(bank));
					continue;
				}

				if (seg->has_holes) {
					UINT8 buf_id	= seg->managed_buf_id;
					UINT32 rd_buf	= MANAGED_BUF(buf_id);
					fla_copy_buffer(sata_rd_buf,
							rd_buf,
							seg->target_sectors);
					free_buffer(buf_id);
				}

				seg->is_done = TRUE;
				continue;
			}

			/* need idle bank */
			signals_set(interesting_signals, SIG_BANK(bank));
			if (!fla_is_bank_idle(bank)) continue;

			/* determine which buffer to use as read buffer */
			UINT32 rd_buf;
			if (check_segment_has_holes(seg)) {
				seg->has_holes = TRUE;

				UINT8 buf_id = buffer_allocate();
				if (buf_id == NULL_BUF_ID) continue;

				seg->managed_buf_id = buf_id;
				rd_buf = MANAGED_BUF(buf_id);
			}
			else {
				rd_buf = sata_rd_buf;
			}

			/* issue flash read cmd */
			UINT8 sect_offset = begin_sector(seg->target_sectors),
			      num_sectors = end_sector(seg->target_sectors)
						- sect_offset;
			fla_read_page(vp, sect_offset, num_sectors, rd_buf);
			seg->is_issued = TRUE;
		}

		if (signals_is_empty(interesting_signals)) break;
		sleep(interesting_signals);
	}
}
/* Update SATA buffer pointers */
phase(SATA_PHASE) {
	g_num_ftl_read_tasks_finished++;

	if (g_next_finishing_task_seq_id == var(seq_id))
		g_next_finishing_task_seq_id++;
	else if (g_num_ftl_read_tasks_finished == g_num_ftl_read_tasks_submitted)
		g_next_finishing_task_seq_id = g_num_ftl_read_tasks_finished;
	else
		end();

	/* safely inform SATA buffer manager to update pointer */
	UINT32 next_read_buf_id = g_next_finishing_task_seq_id % NUM_SATA_RD_BUFFERS;
	SETREG(BM_STACK_RDSET, next_read_buf_id);
	SETREG(BM_STACK_RESET, 0x02);

	ASSERT(g_num_ftl_read_tasks_finished <= g_num_ftl_read_tasks_submitted);
}
}
end_thread_handler

/*
 * Initialiazation
 * */

static thread_handler_id_t registered_handler_id = NULL_THREAD_HANDLER_ID;

void ftl_read_thread_init(thread_t *t, UINT32 lpn, UINT8 sect_offset,
				UINT8 num_sectors)
{
	if (registered_handler_id == NULL_THREAD_HANDLER_ID) {
		registered_handler_id = thread_handler_register(thread_handler);
		ASSERT(registered_handler_id != NULL_THREAD_HANDLER_ID);
	}

	t->handler = registered_handler_id;

	var(seq_id) = g_num_ftl_read_tasks_submitted++;
	var(lpn) = lpn;
	var(sect_offset) = sect_offset;
	var(num_sectors) = num_sectors;
	save_thread_variables(t);
}
