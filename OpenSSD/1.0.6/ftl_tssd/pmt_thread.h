#ifndef __PMT_THREAD_H
#define __PMT_THREAD_H

#include "thread.h"

void pmt_thread_init(thread_t *t);

void pmt_thread_request_enqueue(UINT32 const pmt_idx);

#endif
