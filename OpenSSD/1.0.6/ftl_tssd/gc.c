#include "gc.h"
#include "flash_util.h"
#include "bad_blocks.h"

/* ========================================================================== 
 * Macros and Data Structure 
 * ========================================================================*/

typedef struct _gc_metadata
{
    UINT32 next_write_vpn; // physical page for new write
    UINT32 num_free_pages;
} gc_metadata; 

gc_metadata _metadata[NUM_BANKS];

/* ========================================================================== 
 * Private Functions 
 * ========================================================================*/

void find_next_good_vblk(UINT32 const bank, UINT32 *next_good_vblk) 
{
	while (*next_good_vblk < VBLKS_PER_BANK && bb_is_bad(bank, *next_good_vblk)) 
		(*next_good_vblk)++;

	BUG_ON("no available blocks; need garbage collection", 
			*next_good_vblk == VBLKS_PER_BANK);
}


/* ========================================================================== 
 * Public Functions 
 * ========================================================================*/

void gc_init(void)
{
	UINT32 bank;
	UINT32 user_data_from_vblk = 1;
	UINT32 num_skipped_pages;

	INFO("gc>init", "format flash");
	fu_format(user_data_from_vblk);

	FOR_EACH_BANK(bank) {
		num_skipped_pages = PAGES_PER_VBLK * user_data_from_vblk;
		_metadata[bank].next_write_vpn  = num_skipped_pages;
		_metadata[bank].num_free_pages = PAGES_PER_BANK  
					       - bb_get_num(bank)
					       - num_skipped_pages;
	}
}


UINT32 gc_allocate_new_vpn(UINT32 const bank)
{
	UINT32 next_good_vblk;

	BUG_ON("no more pages; gc is needed, but not implemented yet", 
		gc_get_num_free_pages(bank) == 0);

	/* if need to find a new block */
	if (_metadata[bank].next_write_vpn % PAGES_PER_VBLK == 0) {
		next_good_vblk = _metadata[bank].next_write_vpn / PAGES_PER_VBLK;
		find_next_good_vblk(bank, &next_good_vblk);

		_metadata[bank].next_write_vpn = next_good_vblk * PAGES_PER_VBLK;
	}
	_metadata[bank].num_free_pages--;
	return _metadata[bank].next_write_vpn ++;
}

UINT32 gc_get_num_free_pages(UINT32 const bank)
{
	return _metadata[bank].num_free_pages;
}
