#ifndef __GTD_H
#define __GTD_H
#include "jasmine.h"

/* *
 * GTD = global translation table
 *
 * 		PMT subpage index --> virtual sub page
 *
 * */

/* ===========================================================================
 * Public Interface
 * =========================================================================*/

void gtd_init(void);
void gtd_flush(void);

vsp_t  gtd_get_vsp(UINT32 const pmt_idx);
void   gtd_set_vsp(UINT32 const pmt_idx, vsp_t const vsp);

#endif /* __GTD_H */
