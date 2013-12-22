#ifndef	__FTL_H 
#define __FTL_H

#include "jasmine.h"
#include "dram.h"

/* ========================================================================= *
 * Public API 
 * ========================================================================= */

void ftl_open(void);
#if OPTION_ACL
	void ftl_read(UINT32 const lba, UINT32 const num_sectors, UINT32 const skey);
	void ftl_write(UINT32 const lba, UINT32 const num_sectors, UINT32 const skey);
#else
	void ftl_read(UINT32 const lba, UINT32 const num_sectors);
	void ftl_write(UINT32 const lba, UINT32 const num_sectors);
#endif
void ftl_test_write(UINT32 const lba, UINT32 const num_sectors);
void ftl_flush(void);
void ftl_isr(void);

#endif
