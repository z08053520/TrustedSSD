#ifndef __SCHEDULER_H
#define __SCHEDULER_H

#include "jasmine.h"
#include "thread.h"

/* Send signals to scheduler by modifying this global variable */
signals_t g_scheduler_signals;

void schedule();
void enqueue(thread_t *thread);

#endif
