#ifndef __THREAD_HANDLER_UTIL_H
#define __THREAD_HANDLER_UTIL_H

/*
 * Thread handler -- The function that is executed by a thread
 *
 * Thread handler is a workload for a thread. Thread handler provides mechanism
 * to save and restore stack information so that the thread can resume its
 * work after the thread sleeps and then wakes up.
 *
 * To simulate context switch (yes, it is a simulation, as we can't save the
 * whole stack information), we must be able to save and restore variables
 * used by thread handlers between context switches.
 *
 * */

#include "thread.h"

/*
 * Thread variables
 * */
#define begin_thread_stack		\
	struct thread_stack_t {

#define end_thread_stack		\
	};

#define THREAD_STACK_SIZE		128
UINT8 __thread_stack[THREAD_STACK_SIZE];

#define var(name)	(((thread_stack_t*)__thread_stack)->name)

/*
 * Thread handler
 * */
#define get_thread_handler(name)	(name##_thread_handler)

#define declare_thread_handler(name)				\
		void get_thread_handler(name)(thread_t *__t)

#define begin_thread_handler(name)				\
		declare_thread_handler(name)	{		\
	__begin:						\
			restore_thread_variables(__t);		\
			restore_thread_postion(__t);

#define end_thread_handler()					\
			__t->state = THREAD_STOPPED;		\
		}

/*
 * Thread phase
 * */
#define begin_phase(name)					\
	__##name:{						\
		save_thread_position(__t, name)

#define end_phase(name)						\
		}

#define goto_phase(name)		goto __##name

#define phase2offset(name)		(&&(__##name) - &&(__begin))
#define offset2phase(offset)		(*(&&(__begin) + offset))

/*
 * Context switch
 * */
#define sleep(signals)						\
			__t->wakeup_signals = (signals);		\
			schedule(THREAD_SLEEPING);

#define run_later()						\
			__t->wakeup_signals = 0;		\
			schedule(THREAD_RUNNABLE);

#define schedule(new_state)					\
			__t-->state = (new_state);		\
			save_thread_variables(__t);		\
			return;
/*
 * Save and restore thread information
 * */
#define restore_thread_position(t)	\
		goto offset2phase((t)->handler_last_offset)
#define save_thread_position(t, name)	\
		(t)->handler_last_offset = phase2offset(name)

void restore_thread_variables(thread_t *t);
void save_thread_variables(thread_t *t);
#endif
