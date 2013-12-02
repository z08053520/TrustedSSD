#include "gc.h"
#include "flash_util.h"

/* ========================================================================== 
 * Macros and Data Structure 
 * ========================================================================*/

typedef struct _gc_metadata
{
    UINT32 cur_write_vpn; // physical page for new write
    UINT32 free_blk_cnt;  // total number of free block count
} gc_metadata; 

gc_metadata _metadata[NUM_BANKS];

#define no_free_blk(bank)	(_metadata[bank].free_blk_cnt == 0)

/* ========================================================================== 
 * Private Functions 
 * ========================================================================*/



/* ========================================================================== 
 * Public Functions 
 * ========================================================================*/

void gc_init(void)
{
	UINT32 bank;
	/* write user data stored from block #1 */
	UINT32 from_vblk = 1;

	fu_format(from_vblk);

	FOR_EACH_BANK(bank) {
		_metadata[bank].cur_write_vpn = PAGES_PER_VBLK * from_vblk;
		_metadata[bank].free_blk_cnt  = VLBK_PER_BANK - 1;	
	}
}

UINT32 gc_replace_old_vpn(UINT32 const bank, UINT32 const old_vpn)
{
	return gc_allocate_new_vpn(bank);
}

UINT32 gc_allocate_new_vpn(UINT32 const bank)
{
	UINT32 new_vpn;

	BUG_ON("no available pages", no_free_blk(bank));
	
	new_vpn = _metadata[bank].cur_write_vpn ++;
	if (_metadata[bank].cur_write_vpn % PAGES_PER_VBLK == 0)
		_metadata[bank].free_blk_cnt--;

	return new_vpn;
}
