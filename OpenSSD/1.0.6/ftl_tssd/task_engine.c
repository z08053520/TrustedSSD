#include "task_engine.h"
#include "dram.h"
#include "slab.h"

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

task_t _head;
task_t *head, *tail; 
#define is_engine_idle()	(head == tail)

task_handler_t* task_handlers[MAX_NUM_TASK_TYPES];
UINT8 num_task_types;

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

static 	inline task_res_t run_task(task_t *task, banks_mask_t *idle_banks)
{
	task_handler_t handler = task_handlers[task->type][task->state];
	return (*handler)(task, idle_banks);
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

	/* DEBUG("task_engine", "submit task"); */

	set_next_task(tail, task);
	tail = task;
}

BOOL8 	task_engine_run()
{
	// wait until the new command is accepted by the target bank
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	banks_mask_t idle_banks = probe_idle_banks();

	task_t *pre = head, *task = get_next_task(head);

	while (task) {
		if ((task->waiting_banks & idle_banks) == 0) 
			goto next_task;
		
		task_res_t res;
		do {
			res = run_task(task, &idle_banks);
		} while (res == TASK_CONTINUED);

		if (res == TASK_BLOCKED) break;

		/* Remove task that is done */
		if (res == TASK_FINISHED) {
			set_next_task(pre, get_next_task(task));
			if (task == tail) tail = pre;
			task_deallocate(task);
		}
		/* TASK_PAUSED */
		else {
next_task:
			pre  = task;
		}
		task = get_next_task(pre);
	}
	
	return is_engine_idle();
}
