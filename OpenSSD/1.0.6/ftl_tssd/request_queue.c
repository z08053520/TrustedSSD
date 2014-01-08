#include "request_queue.h"

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

#define reset_req_in_queue(req)		((req)->req_id = NULL_REQ_ID, \
					 (req)->waiting_banks = 0)
static request_in_queue_t _queue[REQ_QUEUE_SIZE];
static UINT8		_queue_head;
static UINT8		_queue_size;

void 	request_queue_init()
{
	g_request_handler = NULL;
	
	_queue_head = 0;
	_queue_size = 0;

	UINT8 i;
	for (i = 0; i < REQ_QUEUE_SIZE; i++)
		reset_req_in_queue(&_queue[i]);
}

BOOL8	request_queue_is_full()
{
	return _queue_size == REQ_QUEUE_SIZE;
}

BOOL8	request_queue_is_empty()
{
	return _queue_size == 0;
}

banks_mask_t	request_queue_get_idle_banks()
{
	banks_mask_t idle_banks = 0;
	UINT8 bank_i;
	for (bank_i = 0; bank_i < NUM_BANKS; bank_i++) {
		if (BSP_FSM(bank_i) == BANK_IDLE) 
			idle_banks |= (1 << bank_i);
	}
	return idle_banks;
}

void	request_queue_accept_new(request_id_t const req_id, 
				 UINT8 const read_or_write,
				 banks_mask_t *idle_banks)
{
	BUG_ON("queue is full; can't accept any new request",
		request_queue_is_full());

	UINT8 queue_tail = (_queue_head + _queue_size) % REQ_QUEUE_SIZE;
	request_in_queue_t *req = & _queue[queue_tail];
	req->req_id = req_id;
	req->rw	    = read_or_write;
	req->waiting_banks = 0;

	(*g_request_handler)(req, idle_banks);
}

#define FOR_EACH_REQUEST_IN_QUEUE(req)					\
	for (queue_idx = _queue_head, 					\
	     queue_end = (_queue_head + _queue_size) % REQ_QUEUE_SIZE,	\
	     req = &_queue[queue_idx]; 				\
	     queue_idx < queue_end; 					\
	     queue_idx = (queue_idx + 1) % REQ_QUEUE_SIZE,		\
	     req = &_queue[queue_idx]) 

void	request_queue_process_old(banks_mask_t *idle_banks)
{
	UINT8 queue_end, queue_idx;
	request_in_queue_t *req;
	// Process old requests
	FOR_EACH_REQUEST_IN_QUEUE(req) {
		if ((*idle_banks) == 0) break;

		if ((req->waiting_banks & (*idle_banks)) == 0) continue;
		
		// process the request if possible
		(*g_request_handler)(req, idle_banks); 
	}

	/* // Update BM read limit */ 
	/* UINT8 done_rd_requests = 0; */
	/* FOR_EACH_REQUEST_IN_QUEUE(req) { */
	/* 	if (req->rw == WR_REQ) continue; */

	/* 	if (req->waiting_banks != 0) break; */
	/* 	done_rd_requests++; */
	/* } */
	/* if (done_rd_requestsa) { */
	/* 	UINT32 new_rd_limit = (GETREG(BM_READ_LIMIT) + done_rd_requests) */ 
	/* 			    % NUM_SATA_RD_BUFFERS; */
	/* 	SETREG(BM_STACK_RDSET, new_rd_limit); */
	/* 	SETREG(BM_STACK_RESET, 0x02); */
	/* } */

	/* // Update BM write limit */
	/* UINT8 done_wr_requests = 0; */
	/* FOR_EACH_REQUEST_IN_QUEUE(req) { */
	/* 	if (req->rw == RD_REQ) continue; */

	/* 	if (req->waiting_banks != 0) break; */
	/* 	done_wr_requests++; */
	/* } */
	/* if (done_wr_requestsa) { */
	/* 	UINT32 new_wr_limit = (GETREG(BM_WRITE_LIMIT) + done_wr_requests) */ 
	/* 			    % NUM_SATA_WR_BUFFERS; */
	/* 	SETREG(BM_STACK_WRSET, new_wr_limit); */
	/* 	SETREG(BM_STACK_RESET, 0x01); */
	/* } */

	// Update queue head and size
	UINT8 done_requests = 0;
	FOR_EACH_REQUEST_IN_QUEUE(req) {
		if (req->waiting_banks != 0) break;
		done_requests++;
	}
	if (done_requests) {
		_queue_head = (_queue_head + done_requests) % REQ_QUEUE_SIZE;
		_queue_size -= done_requests;
	}
}
