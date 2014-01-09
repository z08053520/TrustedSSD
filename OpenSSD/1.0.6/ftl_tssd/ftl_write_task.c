#include "ftl_write_task.h"
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
} ftl_write_task_t;

static UINT8		ftl_write_task_type;

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
	return TASK_CONTINUE;
}

static task_res_t mapping_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	return TASK_CONTINUE;
}

static task_res_t flash_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	return TASK_CONTINUE;
}

static task_res_t finish_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	return TASK_CONTINUE;
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

void ftl_write_task_register()
{
	BOOL8 res = task_engine_register_task_type(
			&ftl_write_task_type, handlers);
	BUG_ON("failed to register FTL write task", res);
}

void ftl_write_task_init(task_t *task, UINT32 const lpn, 
		   	UINT8 const offset, UINT8 const num_sectors)
{
	ftl_write_task_t *write_task = (ftl_write_task_t*) task;

	write_task->type	= ftl_write_task_type;
	write_task->state	= STATE_PREPARATION;

	write_task->lpn 	= lpn;
	write_task->offset	= offset;
	write_task->num_sectors	= num_sectors;
}
