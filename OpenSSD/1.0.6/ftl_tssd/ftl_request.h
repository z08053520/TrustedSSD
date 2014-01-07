#ifndef __FTL_REQUEST_H
#define __FTL_REQUEST_H
#include "request_queue.h"

struct request;

void ftl_request_init();
struct request* allocate_and_init_request(UINT32 const lpn, UINT8 const offset, 
				     	  UINT8  const num_sectors);
request_id_t request_get_id(struct request *req);

#endif
