#include "thread_handler_util.h"

#define THREAD_STACK(i)		(THREAD_BUF_ADDR + THREAD_STACK_SIZE * (i))

void restore_thread_variables(thread_t *t)
{
	thread_id_t tid = thread_id(t);
	mem_copy(__thread_stack, THREAD_STACK(tid), THREAD_STACK_SIZE);
}

void save_thread_variables(thread_t *t)
{
	thread_id_t tid = thread_id(t);
	mem_copy(THREAD_STACK(tid), __thread_stack, THREAD_STACK_SIZE);
}
