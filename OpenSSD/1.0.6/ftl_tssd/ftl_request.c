#include "ftl_request.h"
#include "slab.h"

/* ===========================================================================
 * Macros and Types 
 * =========================================================================*/

/* TODO: use gcc's -fshort-enums to make enum use as less bits as possible */
typedef enum {
	REQ_STATE_NEW,
	REQ_STATE_FINISH,
	NUM_REQ_STATES
} request_state_t;

typedef struct request {
	request_state_t		state;
	UINT32			lpn;
	UINT8			offset;
	UINT8			num_sectors;
} request_t;

/* Allocate memory for requests using slab */
define_slab_interface(request, request_t);
define_slab_implementation(request, request_t, REQ_QUEUE_SIZE);

/* ===========================================================================
 * Request Handler (core of FTL) 
 * =========================================================================*/

static request_t* id2req(request_id_t const id)
{
	return & slab_request_buf[id]; 
}

static void read_handler(request_in_queue_t *req_in_queue, banks_mask_t *idle_banks)
{
	request_t *req = id2req(req_in_queue->req_id);
	switch(req->state) {
	REQ_STATE_NEW:
		break;
	REQ_STATE_FINISH:
		break;
	}
}

static void write_handler(request_in_queue_t *req_in_queue, banks_mask_t *idle_banks)
{
	request_t *req = id2req(req_in_queue->req_id);
	switch(req->state) {
	REQ_STATE_NEW:
		break;
	REQ_STATE_FINISH:
		break;
	}
}

static void ftl_request_handler(request_in_queue_t *req_in_queue, banks_mask_t *idle_banks)
{
	if (req_in_queue->rw == READ)
		read_handler (req_in_queue, idle_banks);
	else
		write_handler(req_in_queue, idle_banks);
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

void ftl_request_init()
{
	init_slab_request();	

	g_request_handler = ftl_request_handler;
}

request_t* allocate_and_init_request(UINT32 const lpn, UINT8 const offset, 
				     UINT8  const num_sectors)
{
	request_t *req = allocate_request();
	req->state = REQ_STATE_NEW;

	req->lpn = lpn;
	req->offset = offset;
	req->num_sectors = num_sectors;
}

request_id_t request_get_id(request_t *req)
{
	BUG_ON("req is null", req == NULL);
	return (slab_request_obj_t)req - slab_request_buf; 
}
