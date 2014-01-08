#ifndef __REQUEST_QUEUE
#define __REQUEST_QUEUE

#include "jasmine.h"

/* ===========================================================================
 * Macros and Types 
 * =========================================================================*/

#define REQ_QUEUE_SIZE		32

typedef UINT16	banks_mask_t;
#if NUM_BANKS > 16
	#error banks_mask_t is not able to represent all banks
#endif

typedef UINT8 request_id_t;
#define NULL_REQ_ID	0xFF

typedef struct _request_in_queue_t {
	request_id_t	req_id;
	UINT8		rw:1;
	UINT8		reserved:7;
	banks_mask_t	waiting_banks;
} request_in_queue_t;

void (*g_request_handler)(request_in_queue_t *req, banks_mask_t *idle_banks);

/* ===========================================================================
 * Public Interface 
 * =========================================================================*/

void 		request_queue_init();
BOOL8		request_queue_is_full();
banks_mask_t	request_queue_get_idle_banks();
void		request_queue_accept_new(request_id_t const req_id, 
					 UINT8 const read_or_write,
					 banks_mask_t *idle_banks);
void		request_queue_process_old(banks_mask_t *idle_banks);
#endif