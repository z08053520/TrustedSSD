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
	sectors_mask_t	target_sectors;
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

#define FOR_EACH_SUB_PAGE(req, lspn, offset_in_sp, num_sectors_in_sp,	\
			  sectors_remain, sector_i)			\
	for (sectors_remain = req->num_sectors,				\
	     sector_i	    = req->offset,				\
	     lspn 	    = lpn2lspn(req->lpn);			\
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
	/* UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_SATA_RD_BUFFERS; */
/* #if OPTION_FTL_TEST == 0 */
	/* while (next_read_buf_id == GETREG(SATA_RBUF_PTR)); */
/* #endif */

	/* sectors_mask_t	target_sectors = init_mask(req->offset, */ 
	/* 					   req->num_sectors) */ 

	/* /1* Try write buffer first for the logical page *1/ */
	/* UINT32 		buf; */
	/* sectors_mask_t	valid_sectors; */
	/* write_buffer_get(req->lpn, &valid_sectors, &buf); */
	/* sectors_mask_t	common_sectors = valid_sectors & target_sectors; */
	/* if (common_sectors) { */
	/* 	buffer_copy(SATA_RD_BUF_PTR(g_ftl_read_buf_id), */
	/* 		    buf, common_sectors); */
		
	/* 	target_sectors &= ~valid_sectors; */
	/* 	/1* In case we can serve all sectors from write buffer *1/ */
	/* 	if (target_sectors == 0) { */
	/* 		req->phase = REQ_PHASE_FINISH; */
	/* 		return TASK_CONTINUE; */ 
	/* 	} */
	/* } */
	
	/* req->target_sectors = target_sectors; */
	/* req->phase = REQ_PHASE_MAPPING; */
	return TASK_CONTINUE;
}

#define banks_mask_has(mask, bank)	(mask & (1 << bank))

static task_res_t mapping_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{

	/* if (!pmt_is_loaded(req->lpn)) { */
	/* 	vsp_t vsp = gtd_get_vsp(pmt_get_index(req->lpn), */
	/* 			        GTD_ZONE_TYPE_PMT); */
	/* 	req->waiting_banks = (1 << vsp.bank); */

	/* 	/1* If required bank is available *1/ */
	/* 	if (banks_mask_has(idle_banks, vsp.bank)) { */
	/* 		fu_read_sub_page(vsp); */
	/* 	} */

	/* 	return TASK_PAUSE; */
	/* } */
	/* req->phase = REQ_PHASE_FLASH; */
	return TASK_CONTINUE;
}

static task_res_t flash_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	/* UINT32 	lspn, sectors_remain, sectors_i; */
	/* UINT8	offset_in_sp, num_sectors_in_sp; */
	/* FOR_EACH_SUB_PAGE(req, lspn, offset_in_sp, num_sectors_in_sp, */
	/* 		  sectors_remain, sector_i) { */
	/* 	UINT8	valid_sectors = 0; */
	/* 	UINT32 	buff = NULL; */
	/* 	write_buffer_get(lspn, &valid_sectors, &buff); */
	/* } */
	return TASK_PAUSE;
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
