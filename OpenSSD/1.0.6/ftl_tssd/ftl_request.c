#include "ftl_request.h"
#include "slab.h"

/* ===========================================================================
 * Macros and Types 
 * =========================================================================*/

extern UINT32 g_ftl_read_buf_id, g_ftl_write_buf_id;

/* TODO: use gcc's -fshort-enums to make enum use as less bits as possible */
typedef enum {
	REQ_PHASE_PREPARATION,
	REQ_PHASE_MAPPING,
	REQ_PHASE_FLASH,
	REQ_PHASE_FINISH,
	NUM_REQ_PHASES
} request_phase_t;

typedef struct request {
	request_phase_t		phase;
	UINT32			lpn;
	UINT8			offset;
	UINT8			num_sectors;
} request_t;

/* Allocate memory for requests using slab */
define_slab_interface(request, request_t);
define_slab_implementation(request, request_t, REQ_QUEUE_SIZE);

/* ===========================================================================
 * Request Handler (Declaration) 
 * =========================================================================*/

#define HANDLER_CONTINUE	0
#define HANDLER_EXIT		1

typedef BOOL8 	(*phase_handler)(request_t *req, 
			 	 banks_mask_t *waiting_banks,
			 	 banks_mask_t *idle_banks);

/* #define declare_phase_handler(name)					\ */
/* 		static BOOL8 name**_phase(				\ */
/* 				request_t* req,				\ */
/* 				banks_mask_t* waiting_banks, 		\ */
/* 				banks_mask_t* idle_banks); */

static BOOL8 read_preparation_phase(request_t*, banks_mask_t*, banks_mask_t*);
static BOOL8 read_mapping_phase(request_t*, banks_mask_t*, banks_mask_t*);
static BOOL8 read_flash_phase(request_t*, banks_mask_t*, banks_mask_t*);
static BOOL8 read_finish_phase(request_t*, banks_mask_t*, banks_mask_t*);

static BOOL8 write_preparation_phase(request_t*, banks_mask_t*, banks_mask_t*);
static BOOL8 write_mapping_phase(request_t*, banks_mask_t*, banks_mask_t*);
static BOOL8 write_flash_phase(request_t*, banks_mask_t*, banks_mask_t*);
static BOOL8 write_finish_phase(request_t*, banks_mask_t*, banks_mask_t*);

phase_handler	handlers_for_read[NUM_REQ_PHASES] = {
	read_preparation_phase,
	read_mapping_phase,
	read_flash_phase,
	read_finish_phase
};

phase_handler	handlers_for_write[NUM_REQ_PHASES] = {
	write_preparation_phase,
	write_mapping_phase,
	write_flash_phase,
	write_finish_phase
};

#define id2req(id)		(& slab_request_buf[(id)])

/* Main handler, invoked by request_queue to handle all requests */
static void ftl_request_handler(request_in_queue_t *req_in_queue, banks_mask_t *idle_banks)
{
	phase_handler* handlers = req_in_queue->rw == READ ?
					handlers_for_read :
					handlers_for_write;
	request_t *req = id2req(req_in_queue->req_id);
	banks_mask_t *waiting_banks = & req_in_queue->waiting_banks;

	BOOL8 res;
	do {
		res = (*(handlers[req->phase]))(req, waiting_banks, idle_banks);
	} while (res == HANDLER_CONTINUE);  
}

/* ===========================================================================
 * Phase Handlers for Read 
 * =========================================================================*/

static BOOL8 read_preparation_phase(request_t*, banks_mask_t*, banks_mask_t*)
{
	return HANDLER_EXIT;
}

static BOOL8 read_mapping_phase(request_t*, banks_mask_t*, banks_mask_t*)
{
	return HANDLER_EXIT;
}

static BOOL8 read_flash_phase(request_t*, banks_mask_t*, banks_mask_t*)
{
	return HANDLER_EXIT;
}

static BOOL8 read_finish_phase(request_t*, banks_mask_t*, banks_mask_t*)
{
	return HANDLER_EXIT;
}

/* ===========================================================================
 * Phase Handlers for Read 
 * =========================================================================*/

static BOOL8 write_preparation_phase(request_t*, banks_mask_t*, banks_mask_t*)
{
	return HANDLER_EXIT;
}

static BOOL8 write_mapping_phase(request_t*, banks_mask_t*, banks_mask_t*)
{
	return HANDLER_EXIT;
}

static BOOL8 write_flash_phase(request_t*, banks_mask_t*, banks_mask_t*)
{
	return HANDLER_EXIT;
}

static BOOL8 write_finish_phase(request_t*, banks_mask_t*, banks_mask_t*)
{
	return HANDLER_EXIT;
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
	req->phase = REQ_PHASE_PREPARATION;

	req->lpn = lpn;
	req->offset = offset;
	req->num_sectors = num_sectors;
}

request_id_t request_get_id(request_t *req)
{
	BUG_ON("req is null", req == NULL);
	return (slab_request_obj_t)req - slab_request_buf; 
}
