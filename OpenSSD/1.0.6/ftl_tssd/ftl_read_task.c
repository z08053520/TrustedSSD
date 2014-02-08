#include "ftl_read_task.h"
#include "dram.h"
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

typedef enum {
	STATE_PREPARATION,
	STATE_MAPPING,
	STATE_FLASH,
	STATE_FINISH,
	NUM_STATES
} state_t;

typedef struct {
	FTL_TASK_PUBLIC_FIELDS
} ftl_read_task_t;

typedef struct {
	sectors_mask_t	task_target_sectors;
	UINT8		num_segments;
	vp_t		vp[SUB_PAGES_PER_PAGE];
	UINT8		offset[SUB_PAGES_PER_PAGE];
	UINT8		num_sectors[SUB_PAGES_PER_PAGE];
	UINT8		has_holes;
	UINT8		cmd_issued;
	UINT8		cmd_done;
} segments_t;

segments_t	_segments;
segments_t	*segments;

#define task_buf_id(task)	(((task)->seq_id) % NUM_SATA_RD_BUFFERS)

static UINT8		ftl_read_task_type;

static task_res_t preparation_state_handler	(task_t*, task_context_t*);
static task_res_t mapping_state_handler		(task_t*, task_context_t*);
static task_res_t flash_state_handler		(task_t*, task_context_t*);
static task_res_t finish_state_handler		(task_t*, task_context_t*);

static task_handler_t handlers[NUM_STATES] = {
	preparation_state_handler,
	mapping_state_handler,
	flash_state_handler,
	finish_state_handler
};

UINT32	g_num_ftl_read_tasks_submitted;
UINT32 	g_num_ftl_read_tasks_finished;
UINT32	g_next_finishing_task_seq_id;

/* ===========================================================================
 *   Help Functions
 * =========================================================================*/

#define FLAG_AUTHENTICATED	1

static task_res_t do_authenticate(ftl_read_task_t *task)
{
	if (!task->uid)	return TASK_CONTINUED;

	task_res_t res = sot_load(task->lpn);
	if (res != TASK_CONTINUED) return res;

	BOOL8 ok = sot_authenticate(task->lpn, task->offset,
				    task->num_sectors, task->uid);
	if (ok) task->flag |= FLAG_AUTHENTICATED;
	task->uid = 0;
	return TASK_CONTINUED;
}

/* ===========================================================================
 *  Task Handlers
 * =========================================================================*/

static task_res_t preparation_state_handler(task_t* _task,
				     	    task_context_t* context)
{
	ftl_read_task_t *task = (ftl_read_task_t*) _task;

	UINT32 read_buf_id	= task_buf_id(task);

	/* uart_printf("preparation > seq_id = %u\r\n", task->seq_id); */

	sectors_mask_t	target_sectors = init_mask(task->offset,
						   task->num_sectors);

	/* Try write buffer first for the logical page */
	UINT32 		buf;
	sectors_mask_t	valid_sectors;
	write_buffer_get(task->lpn, &buf, &valid_sectors);
	if (buf == NULL) goto next_state_mapping;

	sectors_mask_t	common_sectors = valid_sectors & target_sectors;
	if (common_sectors) {
		/* uart_print("found data in write buffer"); */

		fu_copy_buffer(SATA_RD_BUF_PTR(read_buf_id),
			    buf, common_sectors);

		target_sectors &= ~common_sectors;
		/* In case we can serve all sectors from write buffer */
		if (target_sectors == 0) {
			task->state = STATE_FINISH;
			return TASK_CONTINUED;
		}
	}
next_state_mapping:
	segments->task_target_sectors = target_sectors;
	segments->has_holes = 0;
	task->state = STATE_MAPPING;
	return TASK_CONTINUED;
}

static task_res_t mapping_state_handler	(task_t* _task,
					 task_context_t* context)
{
	ftl_read_task_t *task = (ftl_read_task_t*) _task;

#if OPTION_ACL
	task_res_t auth_res = do_authenticate(task);
	if (auth_res == TASK_BLOCKED) return TASK_BLOCKED;
#endif

	task_swap_in(task, segments, sizeof(*segments));
	/* uart_printf("mapping > seq_id = %u\r\n", task->seq_id); */
	task_res_t res = pmt_load(task->lpn * SUB_PAGES_PER_PAGE);
	if (res != TASK_CONTINUED) {
		task_swap_out(task, segments, sizeof(*segments));
		return res;
	}

	UINT8	num_segments = 0;
	/* Iterate each sub-page */
	UINT32	lspn_base = lpn2lspn(task->lpn);
	UINT8	begin_sp  = begin_subpage(segments->task_target_sectors),
		end_sp	  = end_subpage(segments->task_target_sectors);
	UINT8 	sp_i;
	BOOL8	force_new_segment = TRUE;
	for (sp_i = begin_sp; sp_i < end_sp; sp_i++) {
		UINT8 	sector_i = sp_i * SECTORS_PER_SUB_PAGE,
			sp_mask  = (segments->task_target_sectors >> sector_i);
		if (sp_mask == 0) {
			force_new_segment = TRUE;
			continue;
		}

		UINT32	lspn	 = lspn_base + sp_i;
		vp_t	vp;
		pmt_get(lspn, &vp);

		/* uart_printf("pmt fetch: lspn %u --> bank %u, vpn %u\r\n", */
		/* 	    lspn, vp.bank, vp.vpn); */

		/* Gather segment information */
		if (force_new_segment ||
		    vp_not_equal(segments->vp[num_segments - 1], vp)) {
			segments->vp[num_segments] 		= vp;
			segments->offset[num_segments] 	= sector_i;
			segments->num_sectors[num_segments] = 0;
			num_segments++;

			force_new_segment = FALSE;
		}
		segments->num_sectors[num_segments-1] += SECTORS_PER_SUB_PAGE;

		/* if this sub-page has holes, then the segment has holes */
		if (sp_mask != 0xFF)
			mask_set(segments->has_holes, num_segments-1);
	}

	task->waiting_banks 	 = 0;
	segments->num_segments   = num_segments;
	segments->cmd_issued 	 = 0;
	segments->cmd_done	 = 0;
	task->state 	    	 = STATE_FLASH;
	return TASK_CONTINUED;
}

#define is_cmd_issued(seg_i, segments)	mask_is_set(segments->cmd_issued, seg_i)
#define set_cmd_issued(seg_i, segments)	mask_set(segments->cmd_issued, seg_i)
#define is_cmd_done(seg_i, segments)	mask_is_set(segments->cmd_done, seg_i)
#define set_cmd_done(seg_i, segments)	mask_set(segments->cmd_done, seg_i)
#define has_holes(seg_i, segments)	mask_is_set(segments->has_holes, seg_i)

static task_res_t flash_state_handler	(task_t* _task,
					 task_context_t* context)
{
	ftl_read_task_t *task = (ftl_read_task_t*) _task;

#if OPTION_ACL
	do_authenticate(task);
#endif
	/* uart_printf("flash > seq_id = %u\r\n", task->seq_id); */

	task_swap_in(task, segments, sizeof(*segments));

	UINT32 	sata_buf = SATA_RD_BUF_PTR(task_buf_id(task));
	UINT8 	seg_i, num_segments = segments->num_segments;
	for (seg_i = 0; seg_i < num_segments; seg_i++) {
		if (is_cmd_done(seg_i, segments)) continue;

		vp_t		vp  	  = segments->vp[seg_i];
		banks_mask_t 	this_bank = (1 << vp.bank);

		/* uart_printf("seg_i = %u, bank = %u, vpn = %u\r\n", */
		/* 	   seg_i, vp.bank, vp.vpn); */

		/* if the flash read cmd for the segment has been sent */
		if (is_cmd_issued(seg_i, segments)) {
			if ((context->completed_banks & this_bank) == 0) continue;

			/* uart_printf("segment %u is done\r\n", seg_i); */

			if (has_holes(seg_i, segments)) {
				sectors_mask_t segment_target_sectors =
					init_mask(segments->offset[seg_i],
						  segments->num_sectors[seg_i]);
				segment_target_sectors &= segments->task_target_sectors;

				fu_copy_buffer(sata_buf, FTL_RD_BUF(vp.bank),
					       segment_target_sectors);

				/* uart_printf("has holes, copy data from FTL_RD_BUF to SATA_RD_BUF\r\n"); */
			}

			task->waiting_banks &= ~this_bank;
			set_cmd_done(seg_i, segments);
			continue;
		}

		/* Try to reader buffer */
		UINT32 read_buf = NULL;
		read_buffer_get(vp, &read_buf);
		if (read_buf) {
			sectors_mask_t segment_target_sectors =
				init_mask(segments->offset[seg_i],
					  segments->num_sectors[seg_i]);
			segment_target_sectors &= segments->task_target_sectors;

			fu_copy_buffer(sata_buf, read_buf, segment_target_sectors);

			set_cmd_done(seg_i, segments);

			continue;
		}

		/* We have to read from flash */
		task->waiting_banks |= this_bank;

		/* skip if the bank is not available */
		if ((this_bank & context->idle_banks) == 0) continue;

		/* skip if the page is being written */
		if (is_any_task_writing_page(vp)) continue;

		read_buf = has_holes(seg_i, segments) ? FTL_RD_BUF(vp.bank) : sata_buf;
		nand_page_ptread(vp.bank,
				 vp.vpn / PAGES_PER_VBLK,
				 vp.vpn % PAGES_PER_VBLK,
				 segments->offset[seg_i],
				 segments->num_sectors[seg_i],
				 read_buf,
				 RETURN_ON_ISSUE);

		/* uart_printf("issue flash read to bank %u, vpn %u, offset %u, num_sectors %u\r\n", */
		/* 	    vp.bank, vp.vpn, segments->offset[seg_i], segments->num_sectors[seg_i]); */

		set_cmd_issued(seg_i, segments);
		context->idle_banks &= ~this_bank;
	}

	if (task->waiting_banks) {
		task_swap_out(task, segments, sizeof(*segments));
		return TASK_PAUSED;
	}

	task->state = STATE_FINISH;
	return TASK_CONTINUED;
}

static task_res_t finish_state_handler	(task_t* _task,
					 task_context_t* context)
{
	ftl_read_task_t *task = (ftl_read_task_t*) _task;

	/* uart_printf("finish > seq_id = %u\r\n", task->seq_id); */
#if OPTION_FDE
	/* Add decryption overhead */
	fde_decrypt(COPY_BUF(0) + task->offset * BYTES_PER_SECTOR,
		    task->num_sectors, 0);
#endif

#if OPTION_ACL
	task_res_t auth_res = do_authenticate(task);
	if (res != TASK_CONTINUED) return auth_res;

	BOOL8 access_denial = (task->flag & FLAG_AUTHENTICATED) == 0;
	if (access_denial) {
		UINT32 	sata_buf = SATA_RD_BUF_PTR(task_buf_id(task));
		/* TODO: randomize the content of a denied SATA request*/
		mem_set_dram(sata_buf, 0, BYTES_PER_PAGE);
	}
#endif

	g_num_ftl_read_tasks_finished++;

	if (g_next_finishing_task_seq_id == task->seq_id)
		g_next_finishing_task_seq_id++;
	else if (g_num_ftl_read_tasks_finished == g_num_ftl_read_tasks_submitted)
		g_next_finishing_task_seq_id = g_num_ftl_read_tasks_finished;
	else
		return TASK_FINISHED;

	/* safely inform SATA buffer manager to update pointer */
	UINT32 next_read_buf_id = g_next_finishing_task_seq_id % NUM_SATA_RD_BUFFERS;
	SETREG(BM_STACK_RDSET, next_read_buf_id);
	SETREG(BM_STACK_RESET, 0x02);

	BUG_ON("impossible counter", g_num_ftl_read_tasks_finished
			   	   > g_num_ftl_read_tasks_submitted);
	return TASK_FINISHED;
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

void ftl_read_task_register()
{
	uart_printf("sizeof(ftl_read_task_t) = %u, sizeof(task_t) = %u\r\n",
		    sizeof(ftl_read_task_t), sizeof(task_t));

	BUG_ON("ftl read task structure is too large to fit into "
	       "general task structure", sizeof(ftl_read_task_t) > sizeof(task_t));

	g_num_ftl_read_tasks_submitted = 0;
	g_num_ftl_read_tasks_finished  = 0;
	g_next_finishing_task_seq_id   = 0;

	segments = &_segments;

	BOOL8 res = task_engine_register_task_type(
			&ftl_read_task_type, handlers);
	BUG_ON("failed to register FTL read task", res);
}

void ftl_read_task_init(task_t *task,
#if	OPTION_ACL
			UINT32 const uid,
#endif
			UINT32 const lpn,
		   	UINT8 const offset, UINT8 const num_sectors)
{
	ftl_read_task_t *read_task = (ftl_read_task_t*) task;

	read_task->type		= ftl_read_task_type;
	read_task->state	= STATE_PREPARATION;

#if	OPTION_ACL
	read_task->uid		= uid;
#endif
	read_task->lpn 		= lpn;
	read_task->offset	= offset;
	read_task->num_sectors	= num_sectors;
	read_task->flags	= 0;

	/* Assign an unique id to each task */
	read_task->seq_id	= g_num_ftl_read_tasks_submitted++;
}
