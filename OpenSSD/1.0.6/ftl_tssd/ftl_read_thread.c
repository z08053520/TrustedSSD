#include "ftl_thread.h"
#include "thread_handler_util.h"
#include "read_buffer.h"
#include "write_buffer.h"
#include "buffer.h"
#include "fla.h"
#include "pmt.h"
#include "signal.h"
#include "page_lock.h"
#include "dram.h"
#if OPTION_ACL
#include "acl.h"
#endif

#if OPTION_FTL_VERIFY
void ftl_verify(UINT32 const lpn, UINT8 const sect_offset,
		UINT8 const num_sectors, UINT32 const sata_rd_buf);
#endif

/*
 * SATA
 * */

UINT32	g_num_ftl_read_tasks_submitted = 0;
UINT32 	g_num_ftl_read_tasks_finished = 0;
UINT32	g_next_finishing_read_task_seq_id = 0;

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
#if OPTION_ACL
	BOOL8		is_authenticated:1;
#endif
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
	sectors_mask_t sectors = seg->target_sectors;
	ASSERT(sectors != 0);
	UINT8 middle_sectors = end_sector(sectors) - begin_sector(sectors);
	return count_sectors(sectors) < middle_sectors;
}

/*
 * Handler
 * */

begin_thread_variables
	UINT32		seq_id;
	UINT32		lpn;
	sectors_mask_t	target_sectors;
	UINT8		sect_offset;
	UINT8		num_sectors;
#if OPTION_ACL
	user_id_t	uid;
#endif
	UINT8		num_segments;
	segment_t	segments[SUB_PAGES_PER_PAGE];
end_thread_variables

begin_thread_handler
/* Try write buffer first */
phase(BUFFER_PHASE) {
	var(target_sectors) = init_mask(var(sect_offset), var(num_sectors));

	sectors_mask_t buffered_sectors =
		write_buffer_pull(var(lpn), var(target_sectors),
#if OPTION_ACL
				var(uid),
#endif
				sata_rd_buf);
	if (buffered_sectors) {
		var(target_sectors) &= ~buffered_sectors;
		if (var(target_sectors) == 0) goto_phase(SATA_PHASE);
	}
}
phase(LOCK_PHASE) {
	if (lock_page(var(lpn), PAGE_LOCK_READ) != PAGE_LOCK_READ)
		sleep(SIG_LOCK_RELEASED);
}
/* Load PMT page and determine the segments */
phase(PMT_LOAD_PHASE) {
	/* Make sure lpn-->vpn mapping is in PMT */
	if (!pmt_is_loaded(var(lpn))) {
		pmt_load(var(lpn));
		/* wait for LPN to be loaded */
		sleep(SIG_PMT_LOADED);
	}

	/* Iterate each sub-page to make segments */
	UINT8	num_segments = 0;
	segment_t *segments  = var(segments);
	UINT8	begin_sp  = begin_subpage(var(target_sectors)),
		end_sp	  = end_subpage(var(target_sectors));
	for (UINT8 sp_i = begin_sp; sp_i < end_sp; sp_i++) {
		sectors_mask_t sp_sectors = init_mask(
						sp_i * SECTORS_PER_SUB_PAGE,
						SECTORS_PER_SUB_PAGE);
		sectors_mask_t sp_target_sectors = var(target_sectors) & sp_sectors;
		if (sp_target_sectors == 0) continue;

		vp_t	vp;
		pmt_get_vp(var(lpn), sp_i, &vp);

		segment_t *seg; UINT8 seg_i;
		for (seg_i = 0; seg_i < num_segments; seg_i++) {
			seg = &segments[seg_i];
			if (vp_equal(seg->vp, vp)) break;
		}
		if (seg_i == num_segments) {
			seg = &segments[num_segments];
			segment_init(seg, vp);
#if OPTION_ACL
			seg->is_authenticated = acl_authenticate(var(uid), vp);
#endif
			num_segments++;
		}
		seg->target_sectors |= sp_target_sectors;
	}

	var(num_segments) = num_segments;

	/* we can safely unlock the page to read as soon as we know where to
	 * find the pages */
	unlock_page(var(lpn));
}
/* Do flash read */
phase(FLASH_READ_PHASE) {
	signals_t interesting_signals = 0;

	segment_t *seg; UINT8 bank, seg_i;
	for (seg_i = 0; seg_i < var(num_segments); seg_i++) {
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
				buffer_free(buf_id);
			}

			seg->is_done = TRUE;
			continue;
		}

#if OPTION_ACL
		/* fill the segment with 0s if authentication fails */
		if (!seg->is_authenticated) {
			fla_copy_buffer(sata_rd_buf, ALL_ZERO_BUF,
					seg->target_sectors);
			seg->is_done = TRUE;
			continue;
		}
#endif

		/* try to reader buffer */
		UINT32 read_buf = NULL;
		read_buffer_get(seg->vp, &read_buf);
		if (read_buf) {
			fla_copy_buffer(sata_rd_buf, read_buf,
					seg->target_sectors);
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
		fla_read_page(seg->vp, sect_offset, num_sectors, rd_buf);
		seg->is_issued = TRUE;
	}

	if (interesting_signals) sleep(interesting_signals);
}
/* Update SATA buffer pointers */
phase(SATA_PHASE) {
	g_num_ftl_read_tasks_finished++;

#if OPTION_FTL_VERIFY
	ftl_verify(var(lpn), var(sect_offset), var(num_sectors), sata_rd_buf);
#endif

	if (g_next_finishing_read_task_seq_id == var(seq_id))
		g_next_finishing_read_task_seq_id++;
	else if (g_num_ftl_read_tasks_finished == g_num_ftl_read_tasks_submitted)
		g_next_finishing_read_task_seq_id = g_num_ftl_read_tasks_finished;
	else
		end();

	/* safely inform SATA buffer manager to update pointer */
	UINT32 next_read_buf_id = g_next_finishing_read_task_seq_id
				% NUM_SATA_RD_BUFFERS;
	SETREG(BM_STACK_RDSET, next_read_buf_id);
	SETREG(BM_STACK_RESET, 0x02);

	ASSERT(g_num_ftl_read_tasks_finished <= g_num_ftl_read_tasks_submitted);
}
end_thread_handler

/*
 * Initialiazation
 * */

static thread_handler_id_t registered_handler_id = NULL_THREAD_HANDLER_ID;

void ftl_read_thread_init(thread_t *t, const ftl_cmd_t *cmd)
{
	if (registered_handler_id == NULL_THREAD_HANDLER_ID) {
		registered_handler_id = thread_handler_register(
						get_thread_handler());
	}

	t->handler_id = registered_handler_id;

	var(seq_id) = g_num_ftl_read_tasks_submitted++;
	var(lpn) = cmd->lpn;
	var(sect_offset) = cmd->sect_offset;
	var(num_sectors) = cmd->num_sectors;
#if OPTION_ACL
	var(uid) = cmd->uid;
#endif
	init_thread_variables(thread_id(t));
}
