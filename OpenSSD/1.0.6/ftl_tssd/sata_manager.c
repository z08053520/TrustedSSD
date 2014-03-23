#include "sata_manager.h"
#include "dram.h"

/* Id of next task to finish */
static UINT32	next_finish_rid = 0;
/* Id of next task to accept */
static UINT32 	next_accept_rid = 0;
/* Status of pending read tasks: 1 - finished, 0 - running */
static UINT32	read_task_status = 0;

/* Id of next task to finish */
static UINT32	next_finish_wid = 0;
/* Id of next task to accept */
static UINT32 	next_accept_wid = 0;
/* Status of pending write tasks: 1 - finished, 0 - running */
static UINT32	write_task_status = 0;

#define status_set_finished(task_status, tid)	\
		mask_set((task_status), ((tid) % 32))
#define status_is_finished(task_status, tid)	\
		mask_is_set((task_status), ((tid) % 32))
#define status_clear_finished(task_status, tid)	\
		mask_clear((task_status), ((tid) % 32))

BOOL8 sata_manager_can_accept_read_task()
{
	if ((next_accept_rid - next_finish_rid) >= 32) return FALSE;
#if OPTION_FTL_TEST == 0
	UINT32 next_read_buf_id = (next_accept_rid + 1) % NUM_SATA_RD_BUFFERS;
	if (next_read_buf_id == GETREG(SATA_RBUF_PTR)) return FALSE;
#endif
	return TRUE;
}

BOOL8 sata_manager_can_accept_write_task()
{
	if ((next_accept_wid - next_finish_wid) >= 32) return FALSE;
#if OPTION_FTL_TEST == 0
	UINT32 write_buf_id = next_accept_wid % NUM_SATA_WR_BUFFERS;
	if (write_buf_id == GETREG(SATA_WBUF_PTR)) return FALSE;
#endif
	return TRUE;
}

UINT32 sata_manager_accept_read_task()
{
	ASSERT(sata_manager_can_accept_read_task());
	ASSERT(!status_is_finished(read_task_status, next_accept_rid));
	return next_accept_rid++;
}

UINT32 sata_manager_accept_write_task()
{
	ASSERT(sata_manager_can_accept_write_task());
	ASSERT(!status_is_finished(write_task_status, next_accept_wid));
	return next_accept_wid++;
}

void sata_manager_finish_read_task(UINT32 const rid)
{
	ASSERT(rid >= next_finish_rid);
	ASSERT(rid < next_accept_rid);

	status_set_finished(read_task_status, rid);

	UINT8 num_consecutive_finished_tasks = 0;
	while (status_is_finished(read_task_status, next_finish_rid)) {
		status_clear_finished(read_task_status, next_finish_rid);
		next_finish_rid++;
		num_consecutive_finished_tasks++;
	}

	if (!num_consecutive_finished_tasks) return;

	UINT32 next_read_buf_id = next_finish_rid % NUM_SATA_RD_BUFFERS;
	SETREG(BM_STACK_RDSET, next_read_buf_id);
	SETREG(BM_STACK_RESET, 0x02);
}

void sata_manager_finish_write_task(UINT32 const wid)
{
	ASSERT(wid >= next_finish_wid);
	ASSERT(wid < next_accept_wid);

	status_set_finished(write_task_status, wid);

	UINT8 num_consecutive_finished_tasks = 0;
	while (status_is_finished(write_task_status, next_finish_wid)) {
		status_clear_finished(write_task_status, next_finish_wid);
		next_finish_wid++;
		num_consecutive_finished_tasks++;
	}

	if (!num_consecutive_finished_tasks) return;

	UINT32 next_write_buf_id = next_finish_wid % NUM_SATA_WR_BUFFERS;
	SETREG(BM_STACK_WRSET, next_write_buf_id);
	SETREG(BM_STACK_RESET, 0x01);
}

BOOL8 sata_manager_are_all_tasks_finished()
{
	return (next_finish_rid == next_accept_rid)
		&& (next_finish_wid == next_accept_wid);
}
