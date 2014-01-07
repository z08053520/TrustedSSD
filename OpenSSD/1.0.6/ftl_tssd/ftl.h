#ifndef	__FTL_H 
#define __FTL_H

#include "jasmine.h"
#include "dram.h"

/* ========================================================================= *
 * Public API 
 * ========================================================================= */

void ftl_open(void);
void ftl_main(void);
void ftl_flush(void);
void ftl_isr(void);

#endif
