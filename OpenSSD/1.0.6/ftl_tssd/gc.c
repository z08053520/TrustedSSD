#include "gc.h"
#include "fla.h"
#include "bad_blocks.h"

/* ==========================================================================
 * Macros and Data Structure
 * ========================================================================*/

typedef struct _gc_metadata
{
    UINT32	next_vpn[2];
    UINT32	next_free_block;
    UINT32	num_free_blocks;
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
	UINT32 user_data_from_vblk = 1;

	INFO("gc>init", "format flash");
	fla_format_all(user_data_from_vblk);

	UINT8 bank;
	FOR_EACH_BANK(bank) {
		_metadata[bank].next_vpn[0]  	= user_data_from_vblk
						* PAGES_PER_VBLK;
		_metadata[bank].next_vpn[1] 	= (user_data_from_vblk + 1)
						* PAGES_PER_VBLK;
		_metadata[bank].num_free_blocks = VBLKS_PER_BANK
					        - bb_get_num(bank)
					        - 1;
		_metadata[bank].next_free_block	= user_data_from_vblk;
	}
}


UINT32 gc_allocate_new_vpn(UINT32 const bank, BOOL8 const is_sys)
{
	BUG_ON("no more free blocks", gc_get_num_free_blocks(bank) == 0);

	gc_metadata* meta = &_metadata[bank];
	/* if need to find a new block */
	if (meta->next_vpn[is_sys] % PAGES_PER_VBLK == 0) {
		find_next_good_vblk(bank, &meta->next_free_block);
		meta->next_vpn[is_sys] = meta->next_free_block * PAGES_PER_VBLK;
		meta->next_free_block++;
		meta->num_free_blocks--;
	}
	return meta->next_vpn[is_sys] ++;
}

UINT32 gc_get_num_free_blocks(UINT8 const bank)
{
	return _metadata[bank].num_free_blocks;
}
