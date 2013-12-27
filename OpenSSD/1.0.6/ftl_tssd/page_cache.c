#include "page_cache.h"
#include "gc.h"
#include "gtd.h"
#include "dram.h"
#include "flash_util.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

/* * 
 * Bit arragement for key of different page types 
 *	Type	    31  30  29 ... 0	(29 bits for index)
 *	-----------------------------
 * 	PMT	--  1   0    [any]
 * 	SOT	--  1   1    [any]
 * */
#define UINT32_BITS		(sizeof(UINT32) * 8)
#define KEY_PMT_BASE		(1 << (UINT32_BITS - 1))
#define idx2key(idx, type)	(idx | KEY_PMT_BASE)
#define key2idx(key) 		(key - KEY_PMT_BASE)
#define key2type(key)		(key >= KEY_PMT_BASE ? PC_BUF_TYPE_PMT : PC_BUF_TYPE_NUL)

#if OPTION_ACL
#define KEY_SOT_BASE		(KEY_PMT_BASE | (1 << (UINT32_BITS - 2)))
#define idx2key(key, type)	(type == PC_BUF_TYPE_PMT ? key | KEY_PMT_BASE :\
							   key | KEY_SOT_BASE)
#define key2idx(key) 		(key >= KEY_SOT_BASE ? key - KEY_SOT_BASE : \
						       key - KEY_PMT_BASE)
#define key2type(key)		(key >= KEY_SOT_BASE ? PC_BUF_TYPE_SOT : \
					(key >= KEY_PMT_BASE ? 		 \
					 	PC_BUF_TYPE_PMT : 	 \
					 	PC_BUF_TYPE_NUL))
#endif

#define NULL_KEY		0	
#define NULL_TIMESTAMP		0xFFFFFFFF 
#define DIRTY_SIZE		COUNT_BUCKETS(NUM_PC_SUB_PAGES, sizeof(UINT32) * 8)
#define DIRTY_BYTES		(DIRTY_SIZE * sizeof(UINT32))

/* For each cached sub page, we record **key**, **timestamp** and **dirty** */
static UINT32 cached_keys[NUM_PC_SUB_PAGES];
static UINT32 timestamps[NUM_PC_SUB_PAGES];
static UINT32 dirty[DIRTY_SIZE];

static UINT32 num_free_sub_pages = NUM_PC_SUB_PAGES;
static UINT32 current_timestamp  = 0;

#if	OPTION_PERF_TUNING
	UINT32 g_pmt_cache_miss_count = 0;
#if	OPTION_ACL
	UINT32 g_sot_cache_miss_count = 0;
#endif
#endif

/* merge buffer */	
static UINT8  to_be_merged_pages = 0;
static UINT32 to_be_merged_page_indexes[SUB_PAGES_PER_PAGE];

/* Optimize for visiting one PMT/SOT page repeatedly*/
static UINT32 last_key = NULL_KEY, 
	      last_page_idx = SUB_PAGES_PER_PAGE;

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static void page_set_dirty(UINT32 const page_idx)
{
	dirty[page_idx / UINT32_BITS] |= (1 << (page_idx % UINT32_BITS));
}

static void page_reset_dirty(UINT32 const page_idx)
{
	dirty[page_idx / UINT32_BITS] &= ~(1 << (page_idx % UINT32_BITS));
}

static BOOL8 page_is_dirty(UINT32 const page_idx)
{
	return (dirty[page_idx / UINT32_BITS] >> (page_idx % UINT32_BITS)) & 1;
}

static vsp_t get_vsp(UINT32 const idx, pc_buf_type_t const type) {
	vsp_t vsp;

	switch(type) {
	case PC_BUF_TYPE_PMT:
		vsp = gtd_get_vsp(idx, GTD_ZONE_TYPE_PMT);
		break;
#if OPTION_ACL
	case PC_BUF_TYPE_SOT:
		vsp = gtd_get_vsp(idx, GTD_ZONE_TYPE_SOT);
		break;
#endif
	default:
		BUG_ON("invalid type", 1);
	}
	return vsp;
}

static void flush_merge_buffer()
{
	BUG_ON("merge buffer is not full", to_be_merged_pages != 
					   SUB_PAGES_PER_PAGE);

	UINT8  bank = fu_get_idle_bank();
	UINT32 vpn  = gc_allocate_new_vpn(bank);
	vp_t   vp   = {.bank = bank, .vpn  = vpn};
	UINT32 vspn = vpn * SUB_PAGES_PER_PAGE;
	vsp_t  vsp  = {.bank = bank, .vspn = vspn};

	// Iterate merged pages
	UINT8  i;
	for (i = 0; i < SUB_PAGES_PER_PAGE; i++, vsp.vspn++) {
		UINT32 page_idx = to_be_merged_page_indexes[i];
		UINT32 key 	= cached_keys[page_idx];
		UINT32 idx	= key2idx(key);
		pc_buf_type_t type = key2type(key);

		// Update GTD
		switch(type) {
		case PC_BUF_TYPE_PMT:
			gtd_set_vsp(idx, vsp, GTD_ZONE_TYPE_PMT);
			break;
#if OPTION_ACL
		case PC_BUF_TYPE_SOT:
			gtd_set_vsp(idx, vsp, GTD_ZONE_TYPE_SOT);
			break;
#endif
		case PC_BUF_TYPE_NUL:
			BUG_ON("impossible type", 1);
		}

		// Copy to one buffer before writing back to flash
		mem_copy(FTL_BUF(bank) + i * BYTES_PER_SUB_PAGE,
			 PC_SUB_PAGE(page_idx),
			 BYTES_PER_SUB_PAGE);

		// Now this page buffer can be reused
		cached_keys[page_idx] = NULL_KEY;	
	}

	// Write to flash
	fu_write_page(vp, FTL_BUF(bank));

	to_be_merged_pages = 0;
	num_free_sub_pages += SUB_PAGES_PER_PAGE;
}

static UINT32 evict_page()
{
	while (to_be_merged_pages < SUB_PAGES_PER_PAGE) {
		// The LRU page is victim
		UINT32 lru_page_idx = mem_search_min_max(
						 timestamps, 
						 sizeof(UINT32), 
						 NUM_PC_SUB_PAGES,
						 MU_CMD_SEARCH_MIN_SRAM);
		BUG_ON("imposibble timestamp", timestamps[lru_page_idx] == NULL_TIMESTAMP);
		// Prevent this page from evicted again	
		timestamps[lru_page_idx]  = NULL_TIMESTAMP;
		// Evict a clean page and we are done
		if (!page_is_dirty(lru_page_idx)) {
			cached_keys[lru_page_idx] = NULL_KEY;
			num_free_sub_pages++;

			return lru_page_idx;
		}
		// Add this page to merge buffer
		to_be_merged_page_indexes[to_be_merged_pages] = lru_page_idx;
		to_be_merged_pages++;
	}
	
	UINT32 a_free_page_idx = to_be_merged_page_indexes[0];	
	flush_merge_buffer();
	return a_free_page_idx;
}

static UINT32 load_page(UINT32 const idx, pc_buf_type_t const type)
{
	// find a free page
	UINT32 free_page_idx = num_free_sub_pages == 0 ?
					evict_page() :
					mem_search_equ_sram(cached_keys, 
							    sizeof(UINT32),
							    NUM_PC_SUB_PAGES,
							    NULL_KEY);
	BUG_ON("free page not found after evicting", free_page_idx >= NUM_PC_SUB_PAGES);

	cached_keys[free_page_idx] = idx2key(idx, type);
	timestamps[free_page_idx]  = 0;
	page_reset_dirty(free_page_idx);
	num_free_sub_pages--;

	vsp_t vsp = get_vsp(idx, type);
	if (vsp.vspn) {
		fu_read_sub_page(vsp, COPY_BUF(vsp.bank));

		UINT8 sect_offset = vsp.vspn % SUB_PAGES_PER_PAGE * SECTORS_PER_SUB_PAGE; 
		mem_copy(PC_SUB_PAGE(free_page_idx),
			 COPY_BUF(vsp.bank) + sect_offset * BYTES_PER_SECTOR,
			 BYTES_PER_SUB_PAGE);
	}
	else {
		mem_set_dram(PC_SUB_PAGE(free_page_idx), 0, BYTES_PER_SUB_PAGE);
	}
	return free_page_idx;
}

/* ========================================================================= *
 * Public Functions 
 * ========================================================================= */

void page_cache_init(void)
{
	BUG_ON("# of sub pages is not a multiple of 8", 
			NUM_PC_SUB_PAGES % SUB_PAGES_PER_PAGE != 0);
	BUG_ON("Capacity too large", NUM_PC_SUB_PAGES > 255);

	UINT32 num_bytes = sizeof(UINT32) * NUM_PC_SUB_PAGES;
	mem_set_sram(cached_keys, NULL_KEY, 		num_bytes);
	mem_set_sram(timestamps,  NULL_TIMESTAMP, 	num_bytes);
	mem_set_sram(dirty,	  0,			DIRTY_BYTES);
}

void page_cache_load(UINT32 const idx, UINT32 *addr, 
		     pc_buf_type_t const type, BOOL8 const will_modify)
{
	BUG_ON("invalid type", type == PC_BUF_TYPE_NUL);
	
	// try to find cached page given key
	UINT32 key = idx2key(idx, type);
	UINT32 page_idx; 
	if (last_key == key)
		page_idx = last_page_idx;
	else {
		page_idx = mem_search_equ_sram(cached_keys, 
					       sizeof(UINT32), 
					       NUM_PC_SUB_PAGES, key);
		// if not found, load the page into cache
		if (page_idx >= NUM_PC_SUB_PAGES) {
#if	OPTION_PERF_TUNING
			if (type == PC_BUF_TYPE_PMT) g_pmt_cache_miss_count++;
#if	OPTION_ACL
			else if (type == PC_BUF_TYPE_SOT) g_sot_cache_miss_count++;
#endif
#endif
			page_idx = load_page(idx, type);
		}
		last_key = key;
		last_page_idx = page_idx;
	}
	*addr = PC_SUB_PAGE(page_idx);

	// if this page is already in merge buffer, don't modify its state 
	if (timestamps[page_idx] == NULL_TIMESTAMP) 
		return;	
	
	// update timestamp for LRU cache policy
	timestamps[page_idx] = current_timestamp++;
	// handle timestamp overflow
	if (unlikely(current_timestamp == NULL_TIMESTAMP)) {
		mem_set_sram(timestamps, 0, sizeof(UINT32) * NUM_PC_SUB_PAGES);
	
		// we must keep the timestamp of pages to be merged as
		// NULL_TIMESTAMP to prevent them from merged again
		UINT8 merge_page_i = 0;
		while (merge_page_i < to_be_merged_pages) {
			UINT32 merge_page_idx = to_be_merged_page_indexes[merge_page_i];
			timestamps[merge_page_idx] = NULL_TIMESTAMP;

			merge_page_i++;
		}
	}

	// set dirty if to be modified
	if (will_modify) page_set_dirty(page_idx);
}
