#ifndef __TASK_ENGINE_H
#define __TASK_ENGINE_H
#include "jasmine.h"

#define MAX_NUM_TASK_TYPES_LOG2		2
#define MAX_NUM_TASK_STATES_LOG2	6
#define MAX_NUM_TASK_TYPES		(1 << MAX_NUM_TASK_TYPES_LOG2)
#define MAX_NUM_TASK_STATES		(1 << MAX_NUM_TASK_STATES_LOG2)

typedef enum {
	/* CONTINUED: task engine continues to process the current task */
	TASK_CONTINUED,
	/* PAUSED: task engine processes next task as the current task is paused */
	TASK_PAUSED,
	/* BLOCKED: task engine restarts to process earlier tasks */
	TASK_BLOCKED,		
	/* FINISHED: task engine removes a finished task from queue */
	TASK_FINISHED
} task_res_t;

#define NULL_TASK_ID	0xFF
typedef UINT8		task_id_t;

#define ALL_BANKS	0xFFFF
typedef UINT16		banks_mask_t;	

#define TASK_PUBLIC_FIELDS					\
	UINT8		type:MAX_NUM_TASK_TYPES_LOG2;		\
	UINT8		state:MAX_NUM_TASK_STATES_LOG2;		\
	task_id_t	_next_id;				\
	banks_mask_t	waiting_banks;

typedef struct {
	TASK_PUBLIC_FIELDS
	/* TODO: make this smaller */
	UINT8		private_data[32];
} task_t;

typedef task_res_t (*task_handler_t)(task_t *task, banks_mask_t *idle_banks);

BOOL8	task_can_allocate(UINT8 const num_tasks);
task_t* task_allocate();
void	task_deallocate(task_t *task);

void 	task_engine_init();
BOOL8 	task_engine_register_task_type(UINT8 *type, 
				       task_handler_t* handlers);
void 	task_engine_submit(task_t *task);
/* return true if task engine is idle */
BOOL8 	task_engine_run();

#endif
