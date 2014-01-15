#include "ftl_read_task.h"
#include "dram.h"
#include "pmt.h"
#include "read_buffer.h"
#include "write_buffer.h"
#include "flash_util.h"
#include "ftl_task.h"

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
	TASK_PUBLIC_FIELDS
	UINT32		seq_id;
	sectors_mask_t	target_sectors;
	UINT32		lpn;
	UINT8		offset;
	UINT8		num_sectors;
	/* segments */
	UINT8		num_segments;
	vp_t		segment_vp[SUB_PAGES_PER_PAGE];
	UINT8		segment_offset[SUB_PAGES_PER_PAGE];	
	UINT8		segment_num_sectors[SUB_PAGES_PER_PAGE];	
	UINT8		segment_has_holes;
	UINT8		segment_cmd_issued;
	UINT8		segment_cmd_done;
} ftl_read_task_t;

#define task_buf_id(task)	((task->seq_id) % NUM_SATA_RD_BUFFERS)

static UINT8		ftl_read_task_type;

static task_res_t preparation_state_handler	(task_t*, banks_mask_t*);
static task_res_t mapping_state_handler		(task_t*, banks_mask_t*);
static task_res_t flash_state_handler		(task_t*, banks_mask_t*);
static task_res_t finish_state_handler		(task_t*, banks_mask_t*);

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
 *  Private Functions 
 * =========================================================================*/

static void copy_buffer(UINT32 const target_buf, UINT32 const src_buf, 
			sectors_mask_t const valid_sectors)
{
	UINT8 sector_i = 0;
	while (sector_i < SECTORS_PER_PAGE) {
		// find the first sector to copy
		while (sector_i < SECTORS_PER_PAGE && 
		       ((valid_sectors >> sector_i) & 1) == 0) sector_i++;
		if (sector_i == SECTORS_PER_PAGE) break;
		UINT8 begin_sector = sector_i++;

		// find the last sector to copy
		while (sector_i < SECTORS_PER_PAGE && 
		       ((valid_sectors >> sector_i) & 1) == 1) sector_i++;
		UINT8 end_sector = sector_i++;

		mem_copy(target_buf + begin_sector * BYTES_PER_SECTOR,
			 src_buf    + begin_sector * BYTES_PER_SECTOR,
			 (end_sector - begin_sector) * BYTES_PER_SECTOR);
	}
}

/* ===========================================================================
 *  Task Handlers
 * =========================================================================*/

task_res_t preparation_state_handler(task_t* _task, 
				     banks_mask_t* idle_banks)
{
	ftl_read_task_t *task = (ftl_read_task_t*) _task;	

	/* Assign an unique id to each task */
	task->seq_id	= g_num_ftl_read_tasks_submitted++;

	UINT32 read_buf_id	= task_buf_id(task);
#if OPTION_FTL_TEST == 0
	UINT32 next_read_buf_id = (read_buf_id + 1) % NUM_SATA_RD_BUFFERS;
	if (next_read_buf_id == GETREG(SATA_RBUF_PTR)) return TASK_BLOCKED;
#endif

	sectors_mask_t	target_sectors = init_mask(task->offset, 
						   task->num_sectors);

	/* Try write buffer first for the logical page */
	UINT32 		buf;
	sectors_mask_t	valid_sectors;
	write_buffer_get(task->lpn, &buf, &valid_sectors);
	if (buf == NULL) goto next_state_mapping;

	sectors_mask_t	common_sectors = valid_sectors & target_sectors;
	if (common_sectors) {
		copy_buffer(SATA_RD_BUF_PTR(read_buf_id),
			    buf, common_sectors);
		
		target_sectors &= ~common_sectors;
		/* In case we can serve all sectors from write buffer */
		if (target_sectors == 0) {
			task->state = STATE_FINISH;
			return TASK_CONTINUED; 
		}
	}
next_state_mapping:
	task->target_sectors = target_sectors;
	task->segment_has_holes = 0;
	task->state = STATE_MAPPING;
	return TASK_CONTINUED;
}

static task_res_t mapping_state_handler	(task_t* _task, 
					 banks_mask_t* idle_banks)
{
	ftl_read_task_t *task = (ftl_read_task_t*) _task;	

	UINT8	num_segments = 0;
	/* Iterate each sub-page */ 
	UINT32	lspn_base = lpn2lspn(task->lpn);
	UINT8	begin_sp  = begin_subpage(task->target_sectors),
		end_sp	  = end_subpage(task->target_sectors);
	UINT8 	sp_i;
	BOOL8	force_new_segment = TRUE;
	for (sp_i = begin_sp; sp_i < end_sp; sp_i++) {
		UINT8 	sector_i = sp_i * SECTORS_PER_SUB_PAGE,
			sp_mask  = (task->target_sectors >> sector_i);
		if (sp_mask == 0) {
			force_new_segment = TRUE;
			continue;
		}
		
		UINT32	lspn	 = lspn_base + sp_i;
		vp_t	vp;
		pmt_fetch(lspn, &vp);
		
		/* Gather segment information */	
		if (force_new_segment || 
		    vp_not_equal(task->segment_vp[num_segments - 1], vp)) {
			task->segment_vp[num_segments] 		= vp;	
			task->segment_offset[num_segments] 	= sector_i; 
			task->segment_num_sectors[num_segments] = 0; 
			num_segments++;

			force_new_segment = FALSE;
		}
		task->segment_num_sectors[num_segments-1] += SECTORS_PER_SUB_PAGE; 
	
		/* if this sub-page has holes, then the segment has holes */
		if (sp_mask != 0xFF)
			mask_set(task->segment_has_holes, num_segments-1);
	}
	
	task->waiting_banks 	 = 0;
	task->num_segments  	 = num_segments;
	task->segment_cmd_issued = 0;
	task->segment_cmd_done	 = 0;
	task->state 	    	 = STATE_FLASH;
	return TASK_CONTINUED;
}

#define is_cmd_issued(seg_i, task)	mask_is_set(task->segment_cmd_issued, seg_i)
#define set_cmd_issued(seg_i, task)	mask_set(task->segment_cmd_issued, seg_i)
#define is_cmd_done(seg_i, task)	mask_is_set(task->segment_cmd_done, seg_i)
#define set_cmd_done(seg_i, task)	mask_set(task->segment_cmd_done, seg_i)
#define has_holes(seg_i, task)		mask_is_set(task->segment_has_holes, seg_i)

static task_res_t flash_state_handler	(task_t* _task, 
					 banks_mask_t* idle_banks)
{
	ftl_read_task_t *task = (ftl_read_task_t*) _task;	

	UINT32 	sata_buf = SATA_RD_BUF_PTR(task_buf_id(task));
	UINT8 	seg_i, num_segments = task->num_segments;
	for (seg_i = 0; seg_i < num_segments; seg_i++) {
		if (is_cmd_done(seg_i, task)) continue;

		vp_t		vp  	  = task->segment_vp[seg_i];
		banks_mask_t 	this_bank = (1 << vp.bank);

		/* if the flash read cmd for the segment has been sent */
		if (is_cmd_issued(seg_i, task)) {
			if ((*idle_banks & this_bank) == 0) continue;	

			if (has_holes(seg_i, task)) {
				sectors_mask_t segment_target_sectors = 
					init_mask(task->segment_offset[seg_i],
						  task->segment_num_sectors[seg_i]);
				segment_target_sectors &= task->target_sectors;

				copy_buffer(sata_buf, FTL_RD_BUF(vp.bank), 
					    segment_target_sectors); 
			}

			task->waiting_banks &= ~this_bank;
			set_cmd_done(seg_i, task);
			continue;
		}

		/* Try to reader buffer */
		UINT32 read_buf;
		read_buffer_get(vp, &read_buf);
		if (read_buf) {
			sectors_mask_t segment_target_sectors = 
				init_mask(task->segment_offset[seg_i],
					  task->segment_num_sectors[seg_i]);
			segment_target_sectors &= task->target_sectors;

			copy_buffer(sata_buf, read_buf, segment_target_sectors); 
				
			set_cmd_done(seg_i, task);
			continue;
		}

		/* We have to read from flash */
		task->waiting_banks |= this_bank;

		/* if the bank is not avilable for now, skip the segment */
		if ((this_bank & *idle_banks) == 0) continue;

		read_buf = has_holes(seg_i, task) ? FTL_RD_BUF(vp.bank) : sata_buf;
		nand_page_ptread(vp.bank,
				 vp.vpn / PAGES_PER_VBLK,
				 vp.vpn % PAGES_PER_VBLK,
				 task->segment_offset[seg_i],
				 task->segment_num_sectors[seg_i],
				 read_buf,
				 RETURN_ON_ISSUE);

		set_cmd_issued(seg_i, task);
		*idle_banks &= ~this_bank;
	}

	if (task->waiting_banks) return TASK_PAUSED;

	task->state = STATE_FINISH;
	return TASK_CONTINUED;
}

static task_res_t finish_state_handler	(task_t* _task, 
					 banks_mask_t* idle_banks)
{
	ftl_read_task_t *task = (ftl_read_task_t*) _task;	

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

	BOOL8 res = task_engine_register_task_type(
			&ftl_read_task_type, handlers);
	BUG_ON("failed to register FTL read task", res);
}

void ftl_read_task_init(task_t *task, UINT32 const lpn, 
		   	UINT8 const offset, UINT8 const num_sectors)
{
	ftl_read_task_t *read_task = (ftl_read_task_t*) task;

	read_task->type		= ftl_read_task_type;
	read_task->state	= STATE_PREPARATION;

	read_task->lpn 		= lpn;
	read_task->offset	= offset;
	read_task->num_sectors	= num_sectors;
}
