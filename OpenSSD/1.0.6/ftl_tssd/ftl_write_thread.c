#include "ftl_write_thread.h"
#include "thread_handler_util.h"
#include "read_buffer.h"
#include "write_buffer.h"
#include "gc.h"
#include "buffer.h"
#include "fla.h"
#include "pmt.h"
#include "signal.h"
#include "page_lock.h"
#include "dram.h"

/*
 * SATA
 * */

UINT32	g_num_ftl_write_tasks_submitted = 0;
UINT32 	g_num_ftl_write_tasks_finished = 0;
UINT32	g_next_finishing_write_task_seq_id = 0;

#define sata_wr_buf	(SATA_WR_BUF_PTR(var(seq_id) % NUM_SATA_WR_BUFFERS))

/*
 * Handler
 * */

begin_thread_variables
{
	UINT32		seq_id;
	UINT32		lpn;
	UINT8		sect_offset;
	UINT8		num_sectors;
	/* flags */
	BOOL8		cmd_issued;
	union {
		BOOL8		cmd_done;
		BOOL8		pmt_done;
	};
	/* write buffer info */
	vp_t		vp;
	UINT32		buf;
	sectors_mask_t	valid_sectors;
	UINT32		sp_lpn[SUB_PAGES_PER_PAGE];
	vp_t		sp_old_vp[SUB_PAGES_PER_PAGE];
	UINT8		sp_rd_buf_id[SUB_PAGES_PER_PAGE];
}
end_thread_variables

static void copy_subpage_miss_sectors(UINT32 const target_buf,
				UINT32 const src_buf,
				UINT8 const sp_i,
				sectors_mask_t const valid_sectors)
{
	sectors_mask_t sp_sectors =
		init_mask(sp_i * SECTORS_PER_SUB_PAGE, SECTORS_PER_SUB_PAGE);
	sectors_mask_t sp_missing_sectors = sp_sectors & ~valid_sectors;
	fla_copy_buffer(target_buf, src_buf, sp_missing_sectors);
}

begin_thread_handler
{
/* Write buffer if possible */
phase(BUFFER_PHASE) {
	/* put partial page to write buffer */
	if (var(num_sectors)< SECTORS_PER_PAGE) {
		/* don't need to flush write buffer if it is not full*/
		if (!write_buffer_is_full()) {
			write_buffer_put(var(lpn), var(sect_offset),
					var(num_sectors), sata_wr_buf);
			goto_phase(SATA_PHASE);
		}

		UINT8 buf_id = buffer_allocate();
		var(buf) = MANAGED_BUF(buf_id);
		write_buffer_flush(var(buf), var(sp_lpn), &var(valid_sectors));
	}
	/* write whole page to flash directly */
	else {
		var(buf) = sata_wr_buf;
		var(valid_sectors) = FULL_MASK;

		for (UINT8 sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++)
			var(sp_lpn)[sp_i] = var(lpn);

		write_buffer_drop(var(lpn));
	}
}
/* lock pages to write */
phase(LOCK_PHASE) {
	page_lock_type_t lowest_lock = PAGE_LOCK_WRITE;
	UINT32 last_lpn = NULL_LPN;
	for (UINT8 sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT32 lpn = var(sp_lpn)[sp_i];
		if (last_lpn == lpn || lpn == NULL_LPN) continue;
		page_lock_type_t lock = lock_page(lpn, PAGE_LOCK_WRITE);
		last_lpn = lpn;
		if (lock < lowest_lock) lowest_lock = lock;
	}
	if (lowest_lock != PAGE_LOCK_WRITE) run_later();
}
phase(BANK_PHASE) {
	UINT8 idle_bank  = fla_get_idle_bank();
	if (idle_bank >= NUM_BANKS) sleep(SIG_ALL_BANKS);

	var(vp).bank	= idle_bank;
	var(vp).vpn	= gc_allocate_new_vpn(idle_bank, FALSE);

	/* prepare for next phase */
	var(pmt_done)	= 0;
}
phase(MAPPING_PHASE) {
	signals_t interesting_signals = 0;
	for (UINT8 sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++)	{
		if (mask_is_set(var(pmt_done), sp_i)) continue;

		/* skip sub-page that is not in write buffer */
		UINT32 lpn = var(sp_lpn)[sp_i];
		if (lpn == NULL_LPN) {
			mask_set(var(pmt_done), sp_i);
			continue;
		}

		if (!pmt_is_loaded(lpn)) {
			if (pmt_load(lpn))
				signals_set(interesting_signals,
						SIG_PMT_READY);
			else
				signals_set(interesting_signals,
						SIG_PMT_LOADED);
			continue;
		}

		pmt_get_vp(lpn, sp_i, &var(sp_old_vp)[sp_i]);
		pmt_update_vp(lpn, sp_i, var(vp));
		mask_set(var(pmt_done), sp_i);
	}
	if (interesting_signals) sleep(interesting_signals);

	/* prepare for next phase */
	var(cmd_done) = 0;
	var(cmd_issued) = 0;
}
/* Make sure every sub-page has no missing sectors in write buffer */
phase(FLASH_READ_PHASE) {
	signals_t interesting_signals = 0;
	for (UINT8 sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		if (mask_is_set(var(cmd_done), sp_i)) continue;

		UINT8	sp_mask = (var(valid_sectors) >>
					(SECTORS_PER_SUB_PAGE * sp_i));

		/* the whole sub-page is valid or irrelevant */
		if (sp_mask == 0xFF || sp_mask == 0x00) {
			mask_set(var(cmd_done), sp_i);
			continue;
		}

		/* we want to read missing sectors of sub-page from old vp */
		vp_t old_vp = var(sp_old_vp)[sp_i];

		/* if flash read cmd is issued, check whether it is finished */
		if (mask_is_set(var(cmd_issued), sp_i)) {
			if (!fla_is_bank_complete(old_vp.bank)) {
				signals_set(interesting_signals,
						SIG_BANK(old_vp.bank));
				continue;
			}

			UINT8 buf_id = var(sp_rd_buf_id)[sp_i];
			copy_subpage_miss_sectors(var(buf), MANAGED_BUF(buf_id),
							sp_i, var(valid_sectors));
			buffer_free(buf_id);
			mask_set(var(cmd_done), sp_i);
			continue;
		}

		/* try read buffer
		 * for the sub-page has never been written to flash yet */
		UINT32 rd_buf = NULL;
		read_buffer_get(old_vp, &rd_buf);
		if (rd_buf) {
			copy_subpage_miss_sectors(var(buf), rd_buf, sp_i,
							var(valid_sectors));
			mask_set(var(cmd_done), sp_i);
			continue;
		}

		/* we have to issue flash read cmd */
		signals_set(interesting_signals, SIG_BANK(old_vp.bank));

		if (!fla_is_bank_idle(old_vp.bank)) continue;

		UINT8 buf_id = var(sp_rd_buf_id)[sp_i] = buffer_allocate();
		rd_buf = MANAGED_BUF(buf_id);
		fla_read_page(old_vp, sp_i * SECTORS_PER_SUB_PAGE,
				SECTORS_PER_SUB_PAGE, rd_buf);
		mask_set(var(cmd_issued), sp_i);
	}

	if (interesting_signals) sleep(interesting_signals);

	/* prepare for next phase */
	var(cmd_issued) = FALSE;
}
/* Write the buffer to flash */
phase(FLASH_WRITE_PHASE) {
	UINT8 bank = var(vp).bank;
	/* if not issued yet */
	if (!var(cmd_issued)) {
		if (fla_is_bank_idle(bank)) {
			UINT8	begin_i	= begin_subpage(var(valid_sectors))
					* SECTORS_PER_SUB_PAGE,
				end_i	= end_subpage(var(valid_sectors))
					* SECTORS_PER_SUB_PAGE,
				sect_offset = begin_i,
				num_sectors = end_i - begin_i;
			fla_write_page(var(vp), sect_offset,
					num_sectors, var(buf));
			var(cmd_issued) = TRUE;
		}
		sleep(SIG_BANK(bank));
	}
	/* if issued but not finish, sleep */
	if (!fla_is_bank_complete(bank)) sleep(SIG_BANK(bank));

	/* free managed buffer if used */
	UINT8 buf_id = buffer_id(var(buf));
	if (buf_id != NULL_BUF_ID) buffer_free(buf_id);

	/* unlock pages */
	UINT32 last_lpn = NULL_LPN;
	for (UINT8 sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT32 lpn = var(sp_lpn)[sp_i];
		if (last_lpn == lpn || lpn == NULL_LPN) continue;
		unlock_page(lpn);
		last_lpn = lpn;
	}
}
phase(SATA_PHASE) {
	g_num_ftl_write_tasks_finished++;

	if (g_next_finishing_write_task_seq_id == var(seq_id))
		g_next_finishing_write_task_seq_id++;
	else if (g_num_ftl_write_tasks_finished == g_num_ftl_write_tasks_submitted)
		g_next_finishing_write_task_seq_id = g_num_ftl_write_tasks_finished;
	else
		end();

	/* safely inform SATA buffer manager to update pointer */
	UINT32 next_write_buf_id = g_next_finishing_write_task_seq_id
					% NUM_SATA_WR_BUFFERS;
	SETREG(BM_STACK_WRSET, next_write_buf_id);
	SETREG(BM_STACK_RESET, 0x01);
}
}
end_thread_handler

/*
 * Initialiazation
 * */

static thread_handler_id_t registered_handler_id = NULL_THREAD_HANDLER_ID;

void ftl_write_thread_init(thread_t *t, UINT32 lpn, UINT8 sect_offset,
				UINT8 num_sectors)
{
	if (registered_handler_id == NULL_THREAD_HANDLER_ID) {
		registered_handler_id =
			thread_handler_register(get_thread_handler());
	}

	t->handler_id = registered_handler_id;

	var(seq_id) = g_num_ftl_write_tasks_submitted++;
	var(lpn) = lpn;
	var(sect_offset) = sect_offset;
	var(num_sectors) = num_sectors;
	save_thread_variables(thread_id(t));
}
