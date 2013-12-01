#include "gc.h"
/*  
typedef struct _gc_metadata
{
    UINT32 cur_write_vpn; // physical page for new write
    UINT32 cur_miscblk_vpn; // current write vpn for logging the misc. metadata
    UINT32 cur_mapblk_vpn[MAPBLKS_PER_BANK]; // current write vpn for logging the age mapping info.
    UINT32 gc_vblock; // vblock number for garbage collection
    UINT32 free_blk_cnt; // total number of free block count
    UINT32 lpn_list_of_cur_vblock[PAGES_PER_BLK]; // logging lpn list of current write vblock for GC
} gc_metadata; 

gc_metadata _metadata[NUM_BANKS];
*/
void gc_init(void)
{

}

UINT32 gc_replace_old_vpn(UINT32 const bank, UINT32 const old_vpn)
{
	return 0;
}

UINT32 gc_allocate_new_vpn(UINT32 const bank)
{
	return 0;
}
