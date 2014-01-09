#include "ftl_read_task.h"
#include "dram.h"
#include "pmt.h"

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
	UINT32		lpn;
	UINT8		offset;
	UINT8		num_sectors;
	UINT16		g_ftl_read_buf_id;
	sectors_mask_t	target_sectors;
	/* segments */
	vp_t		segment_vp[SUB_PAGES_PER_PAGE];
	UINT8		segment_offset[SUB_PAEGS_PER_PAGE];	
	UINT8		segment_num_sectors[SUB_PAEGS_PER_PAGE];	
	UINT8		num_segments;
} ftl_read_task_t;

static UINT8		ftl_read_task_type;

static task_res_t preparation_state_handler	(task_t*, banks_mask_t*);
static task_res_t mapping_state_handler	(task_t*, banks_mask_t*);
static task_res_t flash_state_handler	(task_t*, banks_mask_t*);
static task_res_t finish_state_handler	(task_t*, banks_mask_t*);

static task_handler_t handlers[NUM_STATES] = {
	preparation_state_handler,
	mapping_state_handler,
	flash_state_handler,
	finish_state_handler
};

/* ===========================================================================
 *  Task Handlers
 * =========================================================================*/

#define lpn2lspn(lpn)		(lpn * SUB_PAGES_PER_PAGE)

#define lspn_offset(lspn)	(lspn % SECTORS_PER_SUB_PAGE)

#define FOR_EACH_SUB_PAGE(task, lspn, offset_in_sp, num_sectors_in_sp,	\
			  sectors_remain, sector_i)			\
	for (sectors_remain = task->num_sectors,				\
	     sector_i	    = task->offset,				\
	     lspn 	    = lpn2lspn(task->lpn);			\
	     sectors_remain > 0 &&					\
	     	  (offset_in_sp       = lspn_offset(lspn), 		\
	     	   num_sectors_in_sp  = 				\
	     	   	(offset_in_sp + sectors_remain 			\
		    	              <= SECTORS_PER_SUB_PAGE ?		\
		       		sectors_remain :			\
		       		SECTORS_PER_SUB_PAGE - offset_in_sp))	\
	     lspn++, 							\
	     sector_i += num_sectors_in_sp,				\
	     sectors_remain -= num_sectors_in_sp)			\

static task_res_t preparation_state_handler(task_t* task, 
					    banks_mask_t* idle_banks)
{
	UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_SATA_RD_BUFFERS;
#if OPTION_FTL_TEST == 0
	while (next_read_buf_id == GETREG(SATA_RBUF_PTR));
#endif
	task->g_ftl_read_buf_id = g_ftl_read_buf_id;

	sectors_mask_t	target_sectors = init_mask(task->offset, 
						   task->num_sectors) 

	/* Try write buffer first for the logical page */
	UINT32 		buf;
	sectors_mask_t	valid_sectors;
	write_buffer_get(task->lpn, &valid_sectors, &buf);
	sectors_mask_t	common_sectors = valid_sectors & target_sectors;
	if (common_sectors) {
		buffer_copy(SATA_RD_BUF_PTR(g_ftl_read_buf_id),
			    buf, common_sectors);
		
		target_sectors &= ~valid_sectors;
		/* In case we can serve all sectors from write buffer */
		if (target_sectors == 0) {
			task->state = STATE_FINISH;
			return TASK_CONTINUE; 
		}
	}
	
	task->target_sectors = target_sectors;
	task->state = STATE_MAPPING;
	return TASK_CONTINUE;
}

#define banks_mask_has(mask, bank)	(mask & (1 << bank))

static task_res_t mapping_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	if (!pmt_is_loaded(task->lpn)) 
		pmt_load(task->lpn);	

	/* Iterate all segments in the logical page */
	UINT8	num_segments = 0;
	UINT32 	lspn, sectors_remain, sector_i;
	UINT8	offset_in_sp, num_sectors_in_sp;
	FOR_EACH_SUB_PAGE(task, lspn, offset_in_sp, num_sectors_in_sp,
			  sectors_remain, sector_i) {
		UINT8 	sp_mask = (task->target_sectors >> sector_i);
		if (sp_mask == 0) continue;

		vp_t	vp;
		pmt_fetch(lspn, &vp);

		/* Try from read buffer */
		UINT32	buf;
		read_buffer_get(vp, &buf);
		if (buf) {
			// TODO: we can make this more efficient by merge 
			// memory copies from the same buffer
			mem_copy(SATA_RD_BUF_PTR(task->g_ftl_read_buf_id) 
					+ sector_i * BYTES_PER_SECTOR,
				 buf + sector_i * BYTES_PER_SECTOR,
				 num_sectors_in_sp * BYTES_PER_SECTOR);
			
			task->target_sectors ~= (0xFFULL << sector_i);
			continue;
		}

		/* Save segment information */	
		if (num_segments == 0 || 
		    task->segment_vp[num_segments - 1] != vp) {
			task->segment_vp[num_segments] = vp;	
			task->segment_offset[num_segments] = sector_i; 
			task->segment_num_sectors[num_segments] = 0; 
			num_segments++;
		}
		task->segment_num_sectors[num_segments-1] += num_sectors_in_sp; 
	}

	task->waiting_banks = 0;
	task->num_segments  = num_segments;	
	task->state 	    = STATE_FLASH;
	return TASK_CONTINUE;
}

static task_res_t flash_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	UINT32 	buf = SATA_RD_BUF_PTR(task->g_ftl_read_buf_id);
	UINT8 	segment_i;
	for (segment_i = 0; segment_i < num_segments; segment_i++) {
		vp_t		vp  = task->vp[segment_i];
		banks_mask_t 	this_bank = (1 << vp.bank);

		/* if the flash read cmd for the segment has been sent */
		if (task->segment_num_sectors == 0) {
			if ((*idle_banks & this_bank))
				waiting_banks ~= this_bank;
			continue;
		}
		
		waiting_banks |= this_bank;

		/* if the bank is not avilable for now, skip the segment */
		if ((this_bank & *idle_banks) == 0) continue;

		nand_page_ptread(vp.bank,
				 vp.vpn / PAGES_PER_VBLK,
				 vp.vpn % PAGES_PER_VBLK,
				 task->segment_offset[segment_i],
				 task->segment_num_sectors[segment_i],
				 buf,
				 RETURN_ON_ISSUE);
		
		*idle_banks &= ~this_bank;
		task->segment_num_sectors[segment_i] == 0;
	}

	if (waiting_banks)
		return TASK_PAUSE;

	task->state = STATE_FINISH;
	return TASK_CONTINUE;
}

static task_res_t finish_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	return TASK_FINISHED;
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

void ftl_read_task_register()
{
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
