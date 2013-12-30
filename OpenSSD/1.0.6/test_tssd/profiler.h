#ifdef OPTION_PROFILING
#ifndef __PROFILER_H
#define __PROFILER_H
#include "jasmine.h"

typedef enum {
	PROFILER_FLASH_FINISH,
	NUM_PROFILER_SUBJECTS
} profiler_subject_t;

void profiler_init();
void profiler_reset		(profiler_subject_t const subject);
void profiler_start_timer	(profiler_subject_t const subject);
void profiler_end_timer		(profiler_subject_t const subject);
UINT32 profiler_get_total_time	(profiler_subject_t const subject);

#endif
#endif
