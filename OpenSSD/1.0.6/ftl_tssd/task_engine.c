#include "task_engine.h"
#include "dram.h"
#include "slab.h"
#include "mem_util.h"

/* ===========================================================================
 *  Macros, Types and Variables  
 * =========================================================================*/

/* Allocate memory for tasks using slab */
define_slab_interface(task, task_t);
define_slab_implementation(task, task_t, MAX_NUM_TASKS);

#define task2id(task)		(task ? (slab_task_obj_t*)task - slab_task_buf : NULL_TASK_ID)
#define id2task(id)		(id != NULL_TASK_ID ? (task_t*)(& slab_task_buf[id]) : NULL)

#define get_next_task(task)		id2task(task->_next_id)
#define set_next_task(task, next_task)	(task->_next_id = task2id(next_task))

static task_t _head;
static task_t *head, *tail; 
#define is_engine_idle()	(head == tail)

/* previous task and current task when task engine is running */
static	task_t *pre_task, *current_task;

static task_handler_t* task_handlers[MAX_NUM_TASK_TYPES];
static UINT8 num_task_types;

static task_context_t context;
static vp_t tasks_writing_vps[MAX_NUM_TASKS];

/* ===========================================================================
 *  Private Functions 
 * =========================================================================*/

static 	banks_mask_t probe_idle_banks()
{
	banks_mask_t idle_banks = 0;
	UINT8 bank_i;
	for (bank_i = 0; bank_i < NUM_BANKS; bank_i++) {
		if (BSP_FSM(bank_i) == BANK_IDLE) 
			idle_banks |= (1 << bank_i);
	}
	return idle_banks;
}

static inline task_res_t process_task(task_t *task)
{
	task_res_t res;
	do {
		task_handler_t handler = task_handlers[task->type][task->state];
		res = (*handler)(task, &context);
	} while (res == TASK_CONTINUED);
	return res;
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

BOOL8	task_can_allocate(UINT8 const num_tasks)
{
	return slab_task_num_free >= num_tasks;
}

task_t* task_allocate()
{
	task_t* task =  slab_allocate_task();
	task->_next_id = NULL_TASK_ID;
	task->waiting_banks = ALL_BANKS;
	task->swapped_out = FALSE;
	return task;
}

void	task_deallocate(task_t *task)
{
	return slab_deallocate_task(task);
}

void	_task_swap_out(task_t *task, void *data, UINT32 const bytes)
{
	if (task->swapped_out) return;

	task_id_t task_id  = task2id(task);	
	UINT32	  swap_buf = TASK_SWAP_BUF(task_id);
	mem_copy(swap_buf, data, bytes);
	task->swapped_out = TRUE;
}

void	_task_swap_in (task_t *task, void *data, UINT32 const bytes)
{
	if (!task->swapped_out) return;

	task_id_t task_id  = task2id(task);	
	UINT32	  swap_buf = TASK_SWAP_BUF(task_id);
	mem_copy(data, swap_buf, bytes);
	task->swapped_out = FALSE;
}

void 	task_engine_init()
{
	init_slab_task();	

	num_task_types = 0;
	mem_set_sram(task_handlers, NULL, sizeof(task_handler_t*) * MAX_NUM_TASK_TYPES);

	tail = head = &_head;
	head->_next_id = NULL_TASK_ID;
	
	context.idle_banks = ALL_BANKS;
	context.completed_banks = 0;

	UINT8	vp_i;
	for (vp_i = 0; vp_i < MAX_NUM_TASKS; vp_i++) {
		tasks_writing_vps[vp_i].bank = NUM_BANKS;
		tasks_writing_vps[vp_i].vpn  = 0;
	}
}

BOOL8 	task_engine_register_task_type(UINT8 *type, 
				       task_handler_t* handlers)
{
	if (num_task_types == MAX_NUM_TASK_TYPES) return TRUE;

	*type = num_task_types++;
	task_handlers[*type] = handlers;
	return FALSE;
}

void 	task_engine_submit(task_t *task)
{
	BUG_ON("task is null!", task == NULL);

	set_next_task(tail, task);
	tail = task;
}

task_res_t	task_engine_insert_and_process(task_t *task)
{
	BUG_ON("insert task when engine is not running",
		pre_task == NULL || current_task == NULL);
	BUG_ON("task is null!", task == NULL);

	task_res_t res = process_task(task);
	if (res == TASK_FINISHED) {
		task_deallocate(task);
		return TASK_FINISHED;
	}

	set_next_task(pre_task, task);
	set_next_task(task, current_task);
	pre_task = task;
	return res;
}

BOOL8 	task_engine_run()
{
beginning:
	/* Wait for all flash commands are accepted */
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	/* Gather events */
	banks_mask_t used_banks = ~context.idle_banks;
	context.idle_banks	= probe_idle_banks();
	context.completed_banks = used_banks & context.idle_banks;

	/* Iterate each task */
	pre_task = head, current_task = get_next_task(head);
	while (current_task) {
		if ((current_task->waiting_banks & context.idle_banks) == 0) 
			goto next_task;
		
		task_res_t res = process_task(current_task);

		if (res == TASK_BLOCKED) {
			/* Start all over again */
			goto beginning;
		}
		else if (res == TASK_FINISHED) { 
			/* Remove task that is done */
			set_next_task(pre_task, get_next_task(current_task));
			if (current_task == tail) tail = pre_task;
			task_deallocate(current_task);
		}
		else { /* TASK_PAUSED */
next_task:
			pre_task  = current_task;
		}
		current_task = get_next_task(pre_task);
	}
	pre_task = current_task = NULL;
	return is_engine_idle();
}

/* The following three functions together prevents pages that is being written to
flash is read by tasks */
BOOL8 is_any_task_writing_page(vp_t const vp)
{
	UINT8	vp_idx			= mem_search_equ_sram(
						tasks_writing_vps,
						sizeof(vp_t),
						MAX_NUM_TASKS,
						vp.as_uint);
	return vp_idx < MAX_NUM_TASKS;
}

void _task_starts_writing_page(vp_t const vp, task_t *task)
{
	UINT8 	vp_idx 			= task2id(task);
	BUG_ON("last writing page is not finished yet", 
		tasks_writing_vps[vp_idx].vpn != 0);
	tasks_writing_vps[vp_idx].bank	= vp.bank;
	tasks_writing_vps[vp_idx].vpn  	= vp.vpn;
}

void _task_ends_writing_page(vp_t const vp, task_t *task)
{
	UINT8 	vp_idx 			= task2id(task);
	BUG_ON("not start writing page yet or finished already", 
		tasks_writing_vps[vp_idx].vpn == 0);
	tasks_writing_vps[vp_idx].bank	= NUM_BANKS;
	tasks_writing_vps[vp_idx].vpn  	= 0;
}

BOOL8	is_there_any_earlier_writing(vp_t const vp)
{
	UINT8 vp_i;
	for (vp_i = 0; vp_i < MAX_NUM_TASKS; vp_i++) {
		if (tasks_writing_vps[vp_i].bank == vp.bank &&
		    tasks_writing_vps[vp_i].vpn < vp.vpn)
			return TRUE;
	}
	return FALSE;
}
