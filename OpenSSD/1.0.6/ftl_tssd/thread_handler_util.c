#include "thread_handler_util.h"

#define THREAD_STACK(i)		(THREAD_SWAP_BUF_ADDR + THREAD_STACK_SIZE * (i))

UINT8 __thread_stack[THREAD_STACK_SIZE] = {0};

void restore_thread_variables(thread_id_t const tid)
{
	mem_copy(__thread_stack, THREAD_STACK(tid), THREAD_STACK_SIZE);
}

void save_thread_variables(thread_id_t const tid)
{
	mem_copy(THREAD_STACK(tid), __thread_stack, THREAD_STACK_SIZE);
}
