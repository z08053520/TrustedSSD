#include "gc.h"
#include "flash_util.h"
#include "bad_blocks.h"

/* ========================================================================== 
 * Macros and Data Structure 
 * ========================================================================*/

typedef struct _gc_metadata
{
    UINT32 cur_write_vpn; // physical page for new write
} gc_metadata; 

gc_metadata _metadata[NUM_BANKS];

#define no_free_blk(bank)	(_metadata[bank].free_blk_cnt == 0)

/* ========================================================================== 
 * Private Functions 
 * ========================================================================*/

void find_next_good_vblk(UINT32 const bank, UINT32 *next_good_vblk) 
{
	while (next_good_vblk < VBLKS_PER_BANK && bb_is_bad(bank, next_good_vblk)) 
		next_good_vblk++;

	BUG_ON("no available blocks; need garbage collection", 
			next_good_vblk == VBLKS_PER_BANK);
}


/* ========================================================================== 
 * Public Functions 
 * ========================================================================*/

void gc_init(void)
{
	UINT32 bank;
	UINT32 user_data_from_vblk = 1;

	fu_format(user_data_from_vblk);

	FOR_EACH_BANK(bank) {
		_metadata[bank].cur_write_vpn = PAGES_PER_VBLK * user_data_from_vblk;
	}
}

UINT32 gc_replace_old_vpn(UINT32 const bank, UINT32 const old_vpn)
{
	return gc_allocate_new_vpn(bank);
}

UINT32 gc_allocate_new_vpn(UINT32 const bank)
{
	UINT32 new_vpn;
	UINT32 next_good_vblk;

	/* if need to find a new block */
	if (_metadata[bank].cur_write_vpn % PAGES_PER_VBLK == 0) {
		next_good_vblk = _metadata[bank].cur_write_vpn / PAGES_PER_VBLK;
		find_next_good_vblk(bank, &next_good_vblk);

		new_vpn = _metadata[bank].cur_write_vpn 
			= next_good_vblk * PAGES_PER_VBLK;
		return new_vpn;
	}
	
	new_vpn = _metadata[bank].cur_write_vpn ++;
	return new_vpn;
}
