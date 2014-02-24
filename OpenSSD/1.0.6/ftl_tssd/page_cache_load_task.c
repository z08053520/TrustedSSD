#include "page_cache_load_task.h"
#include "dram.h"
#include "gtd.h"
#include "page_cache.h"
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
	page_key_t	key;
	UINT32		buf;
	vsp_t		vsp;
} page_cache_load_task_t;

static UINT8	  page_cache_load_task_type;

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

/* ===========================================================================
 *  Task Handlers
 * =========================================================================*/

static task_res_t preparation_state_handler(task_t* _task,
				     	    task_context_t* context)
{
	page_cache_load_task_t *task = (page_cache_load_task_t*)_task;

	/* uart_print("preparation"); */

	page_cache_put(task->key, &(task->buf), PC_FLAG_RESERVED);
	task->state = STATE_MAPPING;
	return TASK_CONTINUED;
}

static task_res_t mapping_state_handler	(task_t* _task,
					 task_context_t* context)
{
	page_cache_load_task_t *task = (page_cache_load_task_t*)_task;

	/* uart_printf("mapping.."); */
	task->vsp = gtd_get_vsp(task->key);

	/* need to load from flash */
	if (task->vsp.vspn != 0) {
		/* uart_print("to flash"); */
		task->state = STATE_FLASH;
		return TASK_CONTINUED;
	}

	/* uart_print("...set mem"); */
	/* page has never been written to flash yet */
	mem_set_dram(task->buf, 0, BYTES_PER_SUB_PAGE);
	page_cache_set_flag(task->key, 0);
	return TASK_FINISHED;
}

static task_res_t flash_state_handler	(task_t* _task,
					 task_context_t* context)
{
	page_cache_load_task_t *task = (page_cache_load_task_t*)_task;

	UINT8	bank = task->vsp.bank;
	if (!banks_has(context->idle_banks, bank)) return TASK_PAUSED;

	vp_t	vp = {.bank = bank, .vpn = task->vsp.vspn / SUB_PAGES_PER_PAGE};
	if (is_any_task_writing_page(vp)) return TASK_PAUSED;

	fu_read_sub_page(task->vsp, FTL_RD_BUF(bank), FU_ASYNC);
	banks_del(context->idle_banks, bank);

	task->state = STATE_FINISH;
	task->waiting_banks = (1 << bank);
	return TASK_PAUSED;
}

static task_res_t finish_state_handler	(task_t* _task,
					 task_context_t* context)
{
	page_cache_load_task_t *task = (page_cache_load_task_t*)_task;

	/* uart_print("finish1"); */
	UINT8	bank = task->vsp.bank;
	if (!banks_has(context->completed_banks, bank))
		return TASK_PAUSED;
	banks_del(context->completed_banks, bank);

	UINT8	sp_offset = task->vsp.vspn % SUB_PAGES_PER_PAGE;
	mem_copy(task->buf,
		 FTL_RD_BUF(bank) + sp_offset * BYTES_PER_SUB_PAGE,
		 BYTES_PER_SUB_PAGE);
	page_cache_set_flag(task->key, 0);
	return TASK_FINISHED;
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

void page_cache_load_task_register()
{
	BUG_ON("load task is too large to fit into general task struct",
		sizeof(page_cache_load_task_t) > sizeof(task_t));

	BOOL8 res = task_engine_register_task_type(
			&page_cache_load_task_type, handlers);
	BUG_ON("failed to register page cache load task", res);
}

void page_cache_load_task_init(task_t *_task, page_key_t const key)
{
	page_cache_load_task_t *task = (page_cache_load_task_t*)_task;

	task->type	= page_cache_load_task_type;
	task->state	= STATE_PREPARATION;
	task->key	= key;
}
