#include "thread_handler_util.h"
#include "dram.h"

#define THREAD_STACK(i)		(THREAD_SWAP_BUF_ADDR + THREAD_STACK_SIZE * (i))

#define THREAD_STACK_SIZE		256
UINT8 thread_stack[THREAD_STACK_SIZE] = {0};
UINT8 * const __thread_stack = thread_stack;

void restore_thread_variables(thread_id_t const tid)
{
	mem_copy(__thread_stack, THREAD_STACK(tid), THREAD_STACK_SIZE);
}

void save_thread_variables(thread_id_t const tid)
{
	mem_copy(THREAD_STACK(tid), __thread_stack, THREAD_STACK_SIZE);
}
