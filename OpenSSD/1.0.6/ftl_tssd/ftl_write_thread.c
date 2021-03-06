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
#include "gc.h"
#if OPTION_ACL
#include "acl.h"
#endif

#define sata_wr_buf	(SATA_WR_BUF_PTR(var(seq_id) % NUM_SATA_WR_BUFFERS))

/*
 * Handler
 * */

begin_thread_variables
	UINT32		seq_id;
	UINT32		lpn;
	UINT8		sect_offset;
	UINT8		num_sectors;
#if OPTION_ACL
	user_id_t	uid;
#endif
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
end_thread_variables

static void copy_subpage_missing_sectors(UINT32 const target_buf,
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
/* Write buffer if possible */
phase(BUFFER_PHASE) {
	var(buf) = NULL;

	/* put partial page to write buffer */
	if (var(num_sectors) < SECTORS_PER_PAGE) {
#if OPTION_ACL
		user_id_t push_buf_uid = var(uid);
#endif
		/* flush write buffer if it is full*/
		if (write_buffer_is_full()) {
			UINT8 managed_buf_id = NULL_BUF_ID;
			write_buffer_flush(&managed_buf_id,
					&var(valid_sectors),
#if OPTION_ACL
					&var(uid),
#endif
					var(sp_lpn));
			ASSERT(managed_buf_id < NUM_MANAGED_BUFFERS);
			ASSERT(var(valid_sectors) != 0);

			var(buf) = MANAGED_BUF(managed_buf_id);
		}

		write_buffer_push(var(lpn), var(sect_offset), var(num_sectors),
#if OPTION_ACL
				push_buf_uid,
#endif
				sata_wr_buf);

		if (var(buf) == NULL) goto_phase(SATA_PHASE);
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
/* Lock pages to write */
phase(LOCK_PHASE) {
	page_lock_type_t lowest_lock = PAGE_LOCK_WRITE;
	UINT32 last_lpn = NULL_LPN;
	for (UINT8 sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT32 lpn = var(sp_lpn)[sp_i];
		if (last_lpn == lpn || lpn == NULL_LPN) continue;
		/* TODO: remember lock that has been acquired to save
		 * rudundant asking for lock that has got */

		page_lock_type_t lock = lock_page(lpn, PAGE_LOCK_WRITE);
		if (lock < lowest_lock) lowest_lock = lock;

		last_lpn = lpn;
	}
	if (lowest_lock != PAGE_LOCK_WRITE) sleep(SIG_LOCK_RELEASED);

	/* prepare for next phase */
	var(pmt_done) = 0;
}
phase(PMT_LOAD_PHASE) {
	signals_t interesting_signals = 0;
	UINT32 last_load_lpn = NULL_LPN;
	for_each_subpage(sp_i) {
		if (mask_is_set(var(pmt_done), sp_i)) continue;

		/* skip sub-page that is not in write buffer */
		UINT32 lpn = var(sp_lpn)[sp_i];
		if (lpn == NULL_LPN) {
			mask_set(var(pmt_done), sp_i);
			continue;
		}

		/* shortcut */
		if (lpn == last_load_lpn) continue;

		if (!pmt_is_loaded(lpn)) {
			pmt_load(lpn);
			last_load_lpn = lpn;
			signals_set(interesting_signals, SIG_PMT_LOADED);
			continue;
		}

		pmt_fix(lpn);
		pmt_get_vp(lpn, sp_i, &var(sp_old_vp)[sp_i]);
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

	UINT8 begin_sp = begin_subpage(var(valid_sectors)),
	      end_sp = end_subpage(var(valid_sectors));
	for (UINT8 sp_i = begin_sp; sp_i < end_sp; sp_i++) {
		if (mask_is_set(var(cmd_done), sp_i)) continue;

		UINT8	sp_mask = (var(valid_sectors) >>
					(SECTORS_PER_SUB_PAGE * sp_i));

		/* if the whole sub-page is valid */
		if (sp_mask == 0xFF) {
			mask_set(var(cmd_done), sp_i);
			continue;
		}
		/* if the whole sub-page is irrelevant */
		if (sp_mask == 0x00) {
			mem_set_dram(var(buf) + sp_i * BYTES_PER_SUB_PAGE,
					0xFFFFFFFF, BYTES_PER_SUB_PAGE);
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
			copy_subpage_missing_sectors(var(buf), MANAGED_BUF(buf_id),
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
			copy_subpage_missing_sectors(var(buf), rd_buf, sp_i,
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
}
phase(BANK_PHASE) {
	UINT8 idle_bank  = fla_get_idle_bank();
	if (idle_bank >= NUM_BANKS) sleep(SIG_ALL_BANKS);

	var(vp).bank	= idle_bank;
	var(vp).vpn	= gc_allocate_new_vpn(idle_bank, FALSE);

#if OPTION_ACL
	acl_authorize(var(uid), var(vp));
#endif
}
phase(PMT_UPDATE_PHASE) {
	for_each_subpage(sp_i) {
		/* skip sub-page that is not in write buffer */
		UINT32 lpn = var(sp_lpn)[sp_i];
		if (lpn == NULL_LPN) continue;

		ASSERT(pmt_is_loaded(lpn));
		pmt_update_vp(lpn, sp_i, var(vp));
		pmt_unfix(lpn);
	}

	/* prepare for next phase */
	var(cmd_issued) = FALSE;
}
/* Write the buffer to flash */
phase(FLASH_WRITE_PHASE) {
	UINT8 bank = var(vp).bank;
	/* if flash write cmd is not issued yet */
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

			/* we can safely unlock pages as soon as flash write
			 * cmd is issued. */
			UINT32 last_lpn = NULL_LPN;
			for_each_subpage(sp_i) {
				UINT32 lpn = var(sp_lpn)[sp_i];
				if (last_lpn == lpn || lpn == NULL_LPN) continue;
				unlock_page(lpn);
				last_lpn = lpn;
			}
		}
		sleep(SIG_BANK(bank));
	}
	/* if issued but not finish, sleep */
	if (!fla_is_bank_complete(bank)) sleep(SIG_BANK(bank));

	/* free managed buffer if used */
	UINT8 buf_id = buffer_id(var(buf));
	if (buf_id != NULL_BUF_ID) buffer_free(buf_id);
}
phase(SATA_PHASE) {
	sata_manager_finish_write_task(var(seq_id));
}
end_thread_handler

/*
 * Initialiazation
 * */

static thread_handler_id_t registered_handler_id = NULL_THREAD_HANDLER_ID;

void ftl_write_thread_init(thread_t *t, const ftl_cmd_t *cmd)
{
	if (registered_handler_id == NULL_THREAD_HANDLER_ID) {
		registered_handler_id =
			thread_handler_register(get_thread_handler());
	}

	t->handler_id = registered_handler_id;

	var(seq_id) = sata_manager_accept_write_task();
	var(lpn) = cmd->lpn;
	var(sect_offset) = cmd->sect_offset;
	var(num_sectors) = cmd->num_sectors;
#if OPTION_ACL
	var(uid) = cmd->uid;
#endif
	init_thread_variables(thread_id(t));
}
