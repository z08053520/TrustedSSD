#include "profiler.h"
#include "mem_util.h"

static UINT32 subject_time[NUM_PROFILER_SUBJECTS];

void profiler_init()
{
	mem_set_sram(subject_time, 0, NUM_PROFILER_SUBJECTS * sizeof(UINT32));
}

void profiler_reset		(profiler_subject_t const subject)
{
	subject_time[subject] = 0;
}

void profiler_start_timer	(profiler_subject_t const subject)
{
	start_interval_measurement(TIMER_CH3, TIMER_PRESCALE_0);
}

void profiler_end_timer		(profiler_subject_t const subject)
{
	UINT32 rtime = 0xFFFFFFFF - GET_TIMER_VALUE(TIMER_CH3);
	subject_time[subject] += rtime;
}

UINT32 profiler_get_total_time	(profiler_subject_t const subject)
{
	
	UINT32 rtime = subject_time[subject];
	rtime = (UINT32)((UINT64)rtime * 2 * 1000000 * PRESCALE_TO_DIV(TIMER_PRESCALE_0) / CLOCK_SPEED);
	return rtime;
}
