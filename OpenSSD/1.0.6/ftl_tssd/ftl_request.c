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
	sectors_mask_t		target_sectors;
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

#define id2req(id)		((request_t*) &(slab_request_buf[(id)]))

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

#define lpn2lspn(lpn)		(lpn * SUB_PAGES_PER_PAGE)

#define lspn_offset(lspn)	(lspn % SECTORS_PER_SUB_PAGE)

#define FOR_EACH_SUB_PAGE(req, lspn, offset_in_sp, num_sectors_in_sp,	\
			  sectors_remain, sector_i)			\
	for (sectors_remain = req->num_sectors,				\
	     sector_i	    = req->offset,				\
	     lspn 	    = lpn2lspn(req->lpn);			\
	     sectors_remain > 0 &&					\
	     	  (offset_in_sp       = lspn_offset(lspn), 		\
	     	   num_sectors_in_sp  = 				\
	     	   	(offset_in_sp + sectors_remain 			\
		    	              <= SECTORS_PER_SUB_PAGE ?		\
		       		sectors_remain :			\
		       		SECTORS_PER_SUB_PAGE - offset_in_sp))	\
	     lspn++, 							\
	     sector_i += num_sectors_in_sp,				\
	     sectors_remain -= num_sectors_in_sp)			\

static BOOL8 read_preparation_phase(request_t* req, 
				    banks_mask_t* waiting_banks, 
				    banks_mask_t* idle_banks)
{
	UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_SATA_RD_BUFFERS;
#if OPTION_FTL_TEST == 0
	while (next_read_buf_id == GETREG(SATA_RBUF_PTR));
#endif

	sectors_mask_t	target_sectors = init_mask(req->offset, 
						   req->num_sectors) 

	/* Try write buffer first for the logical page */
	UINT32 		buf;
	sectors_mask_t	valid_sectors;
	write_buffer_get(req->lpn, &valid_sectors, &buf);
	sectors_mask_t	common_sectors = valid_sectors & target_sectors;
	if (common_sectors) {
		buffer_copy(SATA_RD_BUF_PTR(g_ftl_read_buf_id),
			    buf, common_sectors);
		
		target_sectors &= ~valid_sectors;
		/* In case we can serve all sectors from write buffer */
		if (target_sectors == 0) {
			req->phase = REQ_PHASE_FINISH;
			return HANDLER_CONTINUE; 
		}
	}
	
	req->target_sectors = target_sectors;
	req->phase = REQ_PHASE_MAPPING;
	return HANDLER_CONTINUE;
}

#define banks_mask_has(mask, bank)	(mask & (1 << bank))

static BOOL8 read_mapping_phase(request_t* req, 
				banks_mask_t* waiting_banks, 
				banks_mask_t* idle_banks)
{
	if (!pmt_is_loaded(req->lpn)) {
		vsp_t vsp = gtd_get_vsp(pmt_get_index(req->lpn),
				        GTD_ZONE_TYPE_PMT);
		/* If required bank is not available for now */
		if (!banks_mask_has(idle_banks, vsp.bank)) {
			req->waiting_banks = (1 << vsp.bank);
			return HANDLER_EXIT;
		}

		fu_read_sub_page(vsp);
	}
	req->phase = REQ_PHASE_FLASH;
	return HANDLER_CONTINUE;
}

static BOOL8 read_flash_phase  (request_t* req, 
				banks_mask_t* waiting_banks, 
				banks_mask_t* idle_banks)
{
	UINT32 	lspn, sectors_remain, sectors_i;
	UINT8	offset_in_sp, num_sectors_in_sp;
	FOR_EACH_SUB_PAGE(req, lspn, offset_in_sp, num_sectors_in_sp,
			  sectors_remain, sector_i) {
		UINT8	valid_sectors = 0;
		UINT32 	buff = NULL;
		write_buffer_get(lspn, &valid_sectors, &buff);
	}
	return HANDLER_EXIT;
}

static BOOL8 read_finish_phase (request_t* req, 
				banks_mask_t* waiting_banks, 
				banks_mask_t* idle_banks)
{
	return HANDLER_EXIT;
}

/* ===========================================================================
 * Phase Handlers for Read 
 * =========================================================================*/

static BOOL8 write_preparation_phase(request_t* req, 
				     banks_mask_t* waiting_banks, 
				     banks_mask_t* idle_banks)
{
	return HANDLER_EXIT;
}

static BOOL8 write_mapping_phase(request_t* req, 
				 banks_mask_t* waiting_banks, 
				 banks_mask_t* idle_banks)
{
	return HANDLER_EXIT;
}

static BOOL8 write_flash_phase(request_t* req, 
			       banks_mask_t* waiting_banks, 
			       banks_mask_t* idle_banks)
{
	return HANDLER_EXIT;
}

static BOOL8 write_finish_phase(request_t* req, 
				banks_mask_t* waiting_banks, 
				banks_mask_t* idle_banks)
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
	return req;
}

request_id_t request_get_id(request_t *req)
{
	BUG_ON("req is null", req == NULL);
	return (slab_request_obj_t*)req - slab_request_buf; 
}
