#include "ftl_write_task.h"
#include "dram.h"
#include "gc.h"
#include "pmt.h"
#include "read_buffer.h"
#include "write_buffer.h"
#include "flash_util.h"
#include "ftl_task.h"
#include "fde.h"
#if OPTION_ACL
	#include "sot.h"
#endif

/* ===========================================================================
 *  Macros, types and global variables
 * =========================================================================*/

/* #define DEBUG_WRITE_TASK */
#ifdef DEBUG_WRITE_TASK
	#define debug(format, ...)	uart_print(format, ##__VA_ARGS__)
#else
	#define debug(format, ...)
#endif

typedef enum {
	STATE_PREPARATION,
	STATE_MAPPING,
	STATE_FLASH_READ,
	STATE_FLASH_WRITE,
	STATE_FINISH,
	NUM_STATES
} state_t;

typedef struct {
	FTL_TASK_PUBLIC_FIELDS
} ftl_write_task_t;

typedef struct {
	UINT8		cmd_issued;
	union {
		UINT8		cmd_done;
		UINT8		pmt_done;
	};
	vp_t		vp;
	UINT32		buf;
	sectors_mask_t 	valid_sectors;
	UINT32		lspn[SUB_PAGES_PER_PAGE];
	vp_t		old_vp[SUB_PAGES_PER_PAGE];
} wr_buf_t;

wr_buf_t* const wr_buf = (wr_buf_t*) task_swap_buf;

#define task_buf_id(task)	(((task)->seq_id) % NUM_SATA_WR_BUFFERS)

static UINT8		ftl_write_task_type;

static task_res_t preparation_state_handler	(task_t*, task_context_t*);
static task_res_t mapping_state_handler		(task_t*, task_context_t*);
static task_res_t flash_read_state_handler	(task_t*, task_context_t*);
static task_res_t flash_write_state_handler	(task_t*, task_context_t*);
static task_res_t finish_state_handler		(task_t*, task_context_t*);

static task_handler_t handlers[NUM_STATES] = {
	preparation_state_handler,
	mapping_state_handler,
	flash_read_state_handler,
	flash_write_state_handler,
	finish_state_handler
};

UINT32	g_num_ftl_write_tasks_submitted;
UINT32	g_num_ftl_write_tasks_finished;
UINT32	g_next_finishing_task_seq_id;

/* ===========================================================================
 *  Helper Functions
 * =========================================================================*/

#if OPTION_ACL

static task_res_t do_authorize(ftl_write_task_t *task)
{
	if (!task->uid)	return TASK_CONTINUED;

	task_res_t res = sot_load(task->lpn);
	if (res != TASK_CONTINUED) return res;

	sot_authorize(task->lpn, task->offset, task->num_sectors, task->uid);
	task->uid = 0;
	return TASK_CONTINUED;
}

#endif

/* ===========================================================================
 *  Task Handlers
 * =========================================================================*/

static task_res_t preparation_state_handler(task_t* _task,
					    task_context_t* context)
{
	ftl_write_task_t *task = (ftl_write_task_t*) _task;

	debug("write_task_handler>preparation"
	      "task_id = %u, lpn = %u, offset = %u, num_sectors = %u",
	      task->seq_id, task->lpn, task->offset, task->num_sectors);

	UINT32 write_buf_id	= task_buf_id(task);

	wr_buf->buf		= NULL;
	/* Insert and merge into write buffer */
	if (task->num_sectors < SECTORS_PER_PAGE) {
		// DEBUG
		/* UINT32 buf = SATA_WR_BUF_PTR(write_buf_id); */
		/* uart_print("peek the content of SATA write buffer just before write buffer put"); */
		/* uart_printf("\tSATA write buf id = %u, SATA write buf = %u\r\n", */
		/* 		write_buf_id, buf); */
		/* UINT8 first_sector = task->offset, */
		/*       last_sector   = task->offset + task->num_sectors - 1; */
		/* uart_printf("\tfrom sector %u (%u) to sector %u (%u)\r\n", */
		/* 		first_sector, */
		/* 		read_dram_32(buf + first_sector * BYTES_PER_SECTOR), */
		/* 		last_sector, */
		/* 		read_dram_32(buf + last_sector * BYTES_PER_SECTOR)); */

		if (write_buffer_is_full()) {
			if (context->idle_banks == 0) return TASK_BLOCKED;

			UINT8 idle_bank  = auto_idle_bank(context->idle_banks);
			wr_buf->vp.bank  = idle_bank;
			wr_buf->vp.vpn   = gc_allocate_new_vpn(idle_bank, FALSE);
			wr_buf->buf	 = FTL_WR_BUF(idle_bank);

			write_buffer_flush(wr_buf->buf, wr_buf->lspn,
					   &wr_buf->valid_sectors);
		}

		write_buffer_put(task->lpn, task->offset, task->num_sectors,
				 SATA_WR_BUF_PTR(write_buf_id));
	}
	/* Bypass write buffer and use SATA buffer directly */
	else {
		if (context->idle_banks == 0) return TASK_BLOCKED;

		UINT8 idle_bank  = auto_idle_bank(context->idle_banks);
		wr_buf->vp.bank  = idle_bank;
		wr_buf->vp.vpn   = gc_allocate_new_vpn(idle_bank, FALSE);
		wr_buf->buf	 = SATA_WR_BUF_PTR(write_buf_id);
		wr_buf->valid_sectors = FULL_MASK;

		UINT32 lspn_base = lpn2lspn(task->lpn);
		UINT8 sp_i;
		for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++)
			wr_buf->lspn[sp_i] = lspn_base + sp_i;

		write_buffer_drop(task->lpn);
	}

	if (wr_buf->buf == NULL) {
		task->state = STATE_FINISH;
		return TASK_CONTINUED;
	}

#if OPTION_FDE
	/* Add encryption overhead */
	fde_encrypt(COPY_BUF(0) + task->offset * BYTES_PER_SECTOR,
		    task->num_sectors, 0);
#endif

	task_starts_writing_page(wr_buf->vp, task);
	wr_buf->pmt_done = 0;

	task->state = STATE_MAPPING;
	return TASK_CONTINUED;
}

static task_res_t mapping_state_handler	(task_t* _task,
					 task_context_t* context)
{
	ftl_write_task_t *task = (ftl_write_task_t*) _task;

	debug("write_task_handler>mapping", "task_id = %u", task->seq_id);
#if OPTION_ACL
	task_res_t auth_res = do_authorize(task);
	if (auth_res == TASK_BLOCKED) task_swap_and_return(task, TASK_BLOCKED);
#endif
	task_swap_in(task);

	task_res_t res = TASK_CONTINUED;
	UINT8 sp_i;
	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT32 lspn = wr_buf->lspn[sp_i];
		if (lspn == NULL_LSPN) continue;

		if (mask_is_set(wr_buf->pmt_done, sp_i)) continue;

		task_res_t sp_res = pmt_load(lspn);
		/* TASK_BLOCKED > TASK_PAUSED > TASK_CONTINUED */
		if (sp_res > res)  res = sp_res;

		if (sp_res == TASK_BLOCKED) break;
		else if (sp_res == TASK_PAUSED) continue;

		pmt_get(lspn, & wr_buf->old_vp[sp_i]);
		pmt_update(lspn, wr_buf->vp);
		mask_set(wr_buf->pmt_done, sp_i);
		/* uart_printf("pmt update: lspn %u --> bank %u, vpn %u\r\n", */
		/* 	    lspn, wr_buf->vp.bank, wr_buf->vp.vpn); */
	}

	if (res != TASK_CONTINUED) task_swap_and_return(task, res);

	task->waiting_banks 	= 0;
	task->state 		= STATE_FLASH_READ;
	wr_buf->cmd_issued 	= 0;
	wr_buf->cmd_done 	= 0;
	return TASK_CONTINUED;
}

static task_res_t flash_read_state_handler(task_t* _task,
					   task_context_t* context)
{
	ftl_write_task_t *task = (ftl_write_task_t*) _task;

	debug("write_task_handler>read", "task_id = %u", task->seq_id);
#if OPTION_ACL
	do_authorize(task);
#endif

	task_swap_in(task);

	BUG_ON("no valid sectors in write buffer", wr_buf->valid_sectors == 0);
	UINT8	begin_sp = begin_subpage(wr_buf->valid_sectors),
		end_sp	 = end_subpage(wr_buf->valid_sectors);
	UINT8 	sp_i;
	for (sp_i = begin_sp; sp_i < end_sp; sp_i++) {
		/* TODO: rename macro */
		if (mask_is_set(wr_buf->cmd_done, sp_i)) continue;

		UINT8	sp_mask = (wr_buf->valid_sectors >>
					(SECTORS_PER_SUB_PAGE * sp_i));

		/* all sectors are valid; skip this sub-page */
		if (sp_mask == 0xFF) {
			mask_set(wr_buf->cmd_done, sp_i);
			continue;
		}

		/* fill "hole" with 0xFF..FF */
		if (sp_mask == 0x00) {
			mem_set_dram(wr_buf->buf + sp_i * BYTES_PER_SUB_PAGE,
				     0xFFFFFFFF, BYTES_PER_SUB_PAGE);
			mask_set(wr_buf->cmd_done, sp_i);
			continue;
		}

		BUG_ON("lspn is null but mask is not", wr_buf->lspn[sp_i] == NULL_LSPN);

		UINT8 bank = wr_buf->old_vp[sp_i].bank;
		/* issue flash cmd to fill the paritial sub-page */
		if (!mask_is_set(wr_buf->cmd_issued, sp_i)) {
			vp_t vp    = wr_buf->old_vp[sp_i];

			/* if this sub-page is never written before */
			if (vp.vpn == 0) {
				UINT32 sp_target_buf = wr_buf->buf + BYTES_PER_SUB_PAGE * sp_i;
				void (*segment_handler) (UINT8 const, UINT8 const) =
					/* fill missing sectors from begin_i to end_i */
					lambda (void, (UINT8 const begin_i, UINT8 const end_i) {
						mem_set_dram(sp_target_buf + begin_i * BYTES_PER_SECTOR,
							     0xFFFFFFFF, (end_i - begin_i) * BYTES_PER_SECTOR);
					});
				FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler, sp_mask);

				mask_set(wr_buf->cmd_done, sp_i);
				continue;
			}

			/* bank is needed to issue flash cmd */
			banks_add(task->waiting_banks, bank);

			/* skip if bank is not available */
			if (!banks_has(context->idle_banks, bank)) continue;

			/* skip if the page is being written */
			if (is_any_task_writing_page(vp)) continue;

			banks_del(context->idle_banks, bank);

			vsp_t vsp = {
				.bank = vp.bank,
				.vspn = vp.vpn * SUB_PAGES_PER_PAGE + sp_i
			};
			fu_read_sub_page(vsp, FTL_RD_BUF(bank), FU_ASYNC);
			mask_set(wr_buf->cmd_issued, sp_i);
		}
		/* if flash cmd is done */
		else if (banks_has(context->completed_banks, bank)) {
			UINT32 	sp_target_buf = wr_buf->buf + sp_i * BYTES_PER_SUB_PAGE,
				sp_src_buf    = FTL_RD_BUF(bank)+ sp_i * BYTES_PER_SUB_PAGE;
			void (*segment_handler) (UINT8 const, UINT8 const) =
				lambda (void, (UINT8 const begin_i, const UINT8 end_i) {
					mem_copy(sp_target_buf + begin_i * BYTES_PER_SECTOR,
						 sp_src_buf    + begin_i * BYTES_PER_SECTOR,
						 (end_i - begin_i) * BYTES_PER_SECTOR);
				});
			FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler, sp_mask);

			banks_del(task->waiting_banks, bank);
			mask_set(wr_buf->cmd_done, sp_i);
		}
	}

	/* if some sub-pages are not filled, pause this task */
	if (task->waiting_banks) task_swap_and_return(task, TASK_PAUSED);

	task->state = STATE_FLASH_WRITE;
	wr_buf->cmd_issued = FALSE;
	task->waiting_banks = 1 << wr_buf->vp.bank;
	return TASK_CONTINUED;
}

static task_res_t flash_write_state_handler(task_t* _task,
					    task_context_t* context)
{
	ftl_write_task_t *task 	= (ftl_write_task_t*) _task;

	debug("write_task_handler>write", "task_id = %u", task->seq_id);
#if OPTION_ACL
	do_authorize(task);
#endif

	task_swap_in(task);
	UINT8	bank = wr_buf->vp.bank;

	if (wr_buf->cmd_issued) {
		if (banks_has(context->completed_banks, bank)) {
			task->state = STATE_FINISH;
			return TASK_CONTINUED;
		}
		task_swap_and_return(task, TASK_PAUSED);
	}
	if (!banks_has(context->idle_banks, bank))
		task_swap_and_return(task, TASK_PAUSED);
	if (is_there_any_earlier_writing(wr_buf->vp))
		task_swap_and_return(task, TASK_PAUSED);

	/* offset and num_sectors must align with sub-page */
	UINT32	vpn 		= wr_buf->vp.vpn,
		begin_i		= begin_subpage(wr_buf->valid_sectors)
				* SECTORS_PER_SUB_PAGE,
		end_i		= end_subpage(wr_buf->valid_sectors)
				* SECTORS_PER_SUB_PAGE,
		offset 	   	= begin_i,
		num_sectors 	= end_i - begin_i;
	nand_page_ptprogram(bank,
			    vpn / PAGES_PER_VBLK,
			    vpn % PAGES_PER_VBLK,
			    offset, num_sectors,
			    wr_buf->buf);
	/* uart_printf("!!ptprogram--bank=%u, vpn=%u, offset=%u, num_sectors=%u", */
	/* 	    bank, vpn, offset, num_sectors); */

	banks_del(context->idle_banks, bank);
	wr_buf->cmd_issued = TRUE;
	task_swap_and_return(task, TASK_PAUSED);
}

static task_res_t finish_state_handler	(task_t* _task,
					 task_context_t* context)
{
	ftl_write_task_t *task = (ftl_write_task_t*) _task;

#if OPTION_ACL
	task_res_t auth_res = do_authorize(task);
	if (auth_res != TASK_CONTINUED) task_swap_and_return(auth_res);
#endif

	task_swap_in(task);
	debug("write_task_handler>finish", "task_id = %u", task->seq_id);

	// DEBUG
	/* if (wr_buf->buf) { */
	/* 	uart_print("write buffer = ["); */
	/* 	UINT8	sp_i; */
	/* 	UINT8	count = 0; */
	/* 	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) { */
	/* 		UINT32 lspn = wr_buf->lspn[sp_i]; */
	/* 		if (lspn == NULL_LSPN) continue; */

	/* 		UINT8  sp_offset = (lspn % SUB_PAGES_PER_PAGE */
	/* 					 * SECTORS_PER_SUB_PAGE); */
	/* 		UINT8  sp_mask = wr_buf->valid_sectors >> sp_offset; */
	/* 					; */
	/* 		if (sp_mask == 0) continue; */

	/* 		UINT32 lba  = lspn * SECTORS_PER_SUB_PAGE; */
	/* 		UINT8  sect_i; */
	/* 		for (sect_i = 0; sect_i < SECTORS_PER_SUB_PAGE; sect_i++, lba++) { */
	/* 			if ((sp_mask & (1 << sect_i)) == 0) continue; */

	/* 			uart_printf("\tlba %u(%u): %u\t", */
	/* 				lba, lba % SECTORS_PER_PAGE, */
	/* 				read_dram_32(wr_buf->buf + (sp_offset + sect_i) * BYTES_PER_SECTOR)); */
	/* 			count++; */
	/* 			if (count % 5 == 0)  uart_print(""); */
	/* 		} */
	/* 	} */
	/* 	if (count % 5 != 0)  uart_print(""); */
	/* 	uart_print("]"); */
	/* } */
	/* else { */
	/* 	uart_print("in write buffer"); */
	/* } */

	g_num_ftl_write_tasks_finished++;
	if (wr_buf->buf) task_ends_writing_page(wr_buf->vp, task);

	if (g_next_finishing_task_seq_id == task->seq_id)
		g_next_finishing_task_seq_id++;
	else if (g_num_ftl_write_tasks_finished == g_num_ftl_write_tasks_submitted)
		g_next_finishing_task_seq_id = g_num_ftl_write_tasks_finished;
	else
		return TASK_FINISHED;

	/* safely inform SATA buffer manager to update pointer */
	UINT32 next_write_buf_id = g_next_finishing_task_seq_id % NUM_SATA_WR_BUFFERS;
	SETREG(BM_STACK_WRSET, next_write_buf_id);
	SETREG(BM_STACK_RESET, 0x01);

	BUG_ON("impossible counter", g_num_ftl_write_tasks_finished
			   	   > g_num_ftl_write_tasks_submitted);
	return TASK_FINISHED;
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

void ftl_write_task_register()
{
	uart_printf("sizeof(ftl_write_task_t) = %u, sizeof(task_t) = %u\r\n",
		    sizeof(ftl_write_task_t), sizeof(task_t));

	BUG_ON("ftl write task structure is too large to fit into "
	       "general task structure", sizeof(ftl_write_task_t) > sizeof(task_t));
	BUG_ON("swap buffer required by ftl write task is too large",
		sizeof(wr_buf_t) > TASK_SWAP_BUF_BYTES);

	g_num_ftl_write_tasks_submitted = 0;
	g_num_ftl_write_tasks_finished  = 0;
	g_next_finishing_task_seq_id 	= 0;

	wr_buf = &_wr_buf;

	BOOL8 res = task_engine_register_task_type(
			&ftl_write_task_type, handlers);
	BUG_ON("failed to register FTL write task", res);
}

void ftl_write_task_init(task_t *task,
#if	OPTION_ACL
			user_id_t const uid,
#endif
			UINT32 const lpn,
		   	UINT8 const offset, UINT8 const num_sectors)
{
	ftl_write_task_t *write_task = (ftl_write_task_t*) task;

	write_task->type	= ftl_write_task_type;
	write_task->state	= STATE_PREPARATION;

#if	OPTION_ACL
	// DEBUG
	/* uart_print("ftl_write_task_init uid = %u", uid); */
	write_task->uid		= uid;
#endif
	write_task->lpn 	= lpn;
	write_task->offset	= offset;
	write_task->num_sectors	= num_sectors;
	write_task->flags	= 0;

	/* Assign an unique id to each task */
	write_task->seq_id	= g_num_ftl_write_tasks_submitted++;

	/* uart_printf("task_init: seq_id = %u, lpn = %u, offset= %u, num_sectors = %u, " */
	/* 	    "g_num_ftl_write_tasks_submitted = %u\r\n", */
	/* 	    write_task->seq_id, write_task->lpn, write_task->offset, */
	/* 	    write_task->num_sectors, */
	/* 	    g_num_ftl_write_tasks_submitted); */
}

#if OPTION_FTL_TEST
extern void write_buffer_set_mode(BOOL8 const use_single_buffer);

void ftl_write_task_force_flush()
{
	write_buffer_set_mode(TRUE);

	while (!task_can_allocate(1)) task_engine_run();

	/* Submit a empty write task to force write buffer to flush */
	task_t	*task	= task_allocate();
	UINT32	lpn	= 0;
	UINT8	offset	= 0;
	UINT8	num_sectors = 0;
#if OPTION_ACL
	user_id_t default_uid = 0;
	ftl_write_task_init(task, default_uid, lpn, offset, num_sectors);
#else
	ftl_write_task_init(task, lpn, offset, num_sectors);
#endif
	task_engine_submit(task);

	BOOL8 idle;
	do {
		idle = task_engine_run();
	} while (!idle);

	write_buffer_set_mode(FALSE);
}
#endif
