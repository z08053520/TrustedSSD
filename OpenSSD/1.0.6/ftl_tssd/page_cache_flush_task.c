#include "page_cache_flush_task.h"
#include "dram.h"
#include "gc.h"
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
	vp_t	vp;
} page_cache_flush_task_t;

typedef struct {
	page_key_t	keys[SUB_PAGES_PER_PAGE];
	UINT32		buf;
} merge_buf_t;
merge_buf_t	_merge_buf;
merge_buf_t	*merge_buf = &_merge_buf;

static UINT8	  page_cache_flush_task_type;

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

static UINT32	flush_buf_id = 0;

static task_res_t preparation_state_handler(task_t* _task,
				     	    task_context_t* context)
{
	page_cache_flush_task_t *task = (page_cache_flush_task_t*)_task;

	/* uart_print("flush task: preparation"); */

	merge_buf->buf = PC_FLUSH_BUF(flush_buf_id);
	flush_buf_id = (flush_buf_id + 1) % PC_FLUSH_BUFFERS;

	page_cache_flush(merge_buf->buf, merge_buf->keys);

	task->state = STATE_MAPPING;
	return TASK_CONTINUED;
}

static task_res_t mapping_state_handler	(task_t* _task,
					 task_context_t* context)
{
	page_cache_flush_task_t *task = (page_cache_flush_task_t*)_task;

	task_swap_in(task);

	/* uart_printf("flush task: mapping..."); */

	if (context->idle_banks == 0) task_swap_and_return(task, TASK_PAUSED);

	/* uart_print("to flash"); */
	UINT8	bank	= auto_idle_bank(context->idle_banks);
	UINT32	vpn	= gc_allocate_new_vpn(bank, TRUE);
	task->vp.bank 	= bank;
	task->vp.vpn	= vpn;

	vsp_t	vsp	= {.bank = bank, .vspn = vpn * SUB_PAGES_PER_PAGE};
	UINT8	sp_i;
	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		page_key_t key = merge_buf->keys[sp_i];
		gtd_set_vsp(key, vsp);
		vsp.vspn++;
	}
	task->state = STATE_FLASH;
	return TASK_CONTINUED;
}

static task_res_t flash_state_handler	(task_t* _task,
					 task_context_t* context)
{
	page_cache_flush_task_t *task = (page_cache_flush_task_t*)_task;

	/* uart_print("flush task: flash... paused"); */

	fu_write_page(task->vp, merge_buf->buf);
	UINT8 bank = task->vp.bank;
	banks_del(context->idle_banks, bank);

	task->state = STATE_FINISH;
	task->waiting_banks = (1 << bank);
	return TASK_PAUSED;
}

static task_res_t finish_state_handler	(task_t* _task,
					 task_context_t* context)
{
	page_cache_flush_task_t *task = (page_cache_flush_task_t*)_task;

	/* uart_printf("flush task: finish..."); */
	if (!banks_has(context->completed_banks, task->vp.bank)) {
		/* uart_print("not completed"); */
		return TASK_PAUSED;
	}
	/* uart_print("completed!!!"); */
	return TASK_FINISHED;
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

void page_cache_flush_task_register()
{
	BUG_ON("flush task is too large to fit into general task struct",
		sizeof(page_cache_flush_task_t) > sizeof(task_t));

	BOOL8 res = task_engine_register_task_type(
			&page_cache_flush_task_type, handlers);
	BUG_ON("failed to register page cache flush task", res);
}

void page_cache_flush_task_init(task_t *_task)
{
	page_cache_flush_task_t *task = (page_cache_flush_task_t*)_task;

	task->type	= page_cache_flush_task_type;
	task->state	= STATE_PREPARATION;
}
