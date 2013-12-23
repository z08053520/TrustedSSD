#include "page_cache.h"
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
#define KEY_PMT_BASE		(1 << (sizeof(UINT32) - 1))
#define idx2key(idx, type)	(idx | KEY_PMT_BASE)
#define key2idx(key) 		(key - KEY_PMT_BASE)
#define key2type(key)		(key >= KEY_PMT_BASE ? PC_BUF_TYPE_PMT : PC_BUF_TYPE_NUL)

#if OPTION_ACL
#define KEY_SOT_BASE		(KEY_PMT_BASE | (1 << (sizeof(UINT32) - 2)))
#define idx2key(key, type)	(type == PC_BUF_TYPE_PMT ? key | KEY_PMT_BASE :\
							   key | KEY_SOT_BASE)
#define key2idx(key) 		(key >= KEY_SOT_BASE ? key - KEY_SOT_BASE : \
						       key - KEY_PMT_BASE)
#define key2type(key)		(key >= KEY_SOT_BASE ? PC_BUF_TYPE_SOT : \
					(key >= KEY_PMT_BASE ? 		 \
					 	PC_BUF_TYPE_PMT : 	 \
					 	PC_BUF_TYPE_NUL))
#endif

/* For each cached sub page, we record **key**, **timestamp** and **dirty***/
static UINT32 cached_keys[NUM_PC_SUB_PAGES];
static UINT32 timestamps[NUM_PC_SUB_PAGES];
#define DIRTY_SIZE		COUNT_BUCKETS(NUM_PC_SUB_PAGES, sizeof(UITN32))
#define DIRTY_BYTES		(DIRTY_SIZE * sizeof(UINT32))
static UINT32 dirty[DIRTY_SIZE];

static UINT32 num_free_sub_pages = NUM_PC_SUB_PAGES;
static UINT32 current_timestamp = 0;

#if	OPTION_PERF_TUNING
	UINT32 g_pmt_cache_miss_count = 0;
#if	OPTION_ACL
	UINT32 g_sot_cache_miss_count = 0;
#endif
#endif

typedef struct _merge_buffer_page {
	UINT32 index;
	pc_buf_type_t type;
} merge_buffer_page_t;
static merge_buffer_page_t 	merge_buf[SUB_PAGES_PER_PAGE];
static UINT8			merge_buf_size = 0;

/* Optimize for visiting one PMT/SOT page repeatedly*/
static UINT32 last_key = 0, last_page_idx = 0;

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static void page_set_dirty(UINT32 const page_idx)
{
	dirty[page_idx / sizeof(UINT32)] |= (1 << (page_idx % sizeof(UINT32)));
}

static void page_reset_dirty(UINT32 const page_idx)
{
	dirty[page_idx / sizeof(UINT32)] &= ~(1 << (page_idx % sizeof(UINT32)));
}

static BOOL8 page_is_dirty(UINT32 const page_idx)
{
	return (dirty[page_idx / sizeof(UINT32)] >> (page_idx % sizeof(UINT32))) & 1;
}

static UINT32 get_vspn(UINT32 const idx, pc_buf_type_t const type) {
	UINT32 vspn;

	switch(type) {
	case PC_BUF_TYPE_PMT:
		vspn = gtd_get_vspn(idx, GTD_ZONE_TYPE_PMT);
		break;
#if OPTION_ACL
	case PC_BUF_TYPE_SOT:
		vspn = gtd_get_vspn(idx, GTD_ZONE_TYPE_SOT);
		break;
#endif
	default:
		BUG_ON("invalid type", 1);
	}
	return vspn;
}

static void flush_merge_buffer()
{
	BUG_ON("merge buffer is not full", merge_buf_size != SUB_PAGES_PER_PAGE);

	// TODO: don't need to specify bank
	UINT32 vpn  = gc_allocate_new_vpn(0);
	UINT32 vspn = vpn * SUB_PAGES_PER_PAGE;

	// Update GTD
	UINT8  i;
	UINT32 idx;
	pc_buf_type_t type;
	for (i = 0; i < SUB_PAGES_PER_PAGE; i++, vspn++) {
		idx  = merge_buf[i].index;
		type = merge_buf[i].type;

		switch(type) {
		case PC_BUF_TYPE_PMT:
			gtd_set_vspn(idx, GTD_ZONE_TYPE_PMT, vspn);
			break;
#if OPTION_ACL
		case PC_BUF_TYPE_SOT:
			gtd_set_vspn(idx, GTD_ZONE_TYPE_SOT, vspn);
			break;
#endif
		defeault:
			BUG_ON("impossible type", 1);
		}
	}

	// Write to flash
	fu_write_page(vpn, PC_MERGE_BUF);
	merge_buf_size = 0;
}

static void evict_page()
{
	// The LRU page is victim
	UINT32 lru_page_idx = mem_search_min_max(timestamps, 
						 sizeof(UINT32), 
						 NUM_PC_SUB_PAGES,
						 MU_CMD_SEARCH_MIN_SRAM);

	// Add victim page to merge buffer if dirty
	if (page_is_dirty(lru_page_idx)) {
		if (merge_buf_size == SUB_PAGES_PER_PAGE) flush_merge_buffer();

		UINT32 key  	    		= cached_keys[lru_page_idx];
		merge_buf[merge_buf_size].index = key2idx(key);
		merge_buf[merge_buf_size].type  = key2type(key);
		mem_copy_copy(PC_MERGE_BUF + merge_buf_size * BYTES_PER_SUB_PAGES,
			      PC_SUB_PAGE(lru_page_idx),
			      BYTES_PER_SUB_PAGE);
		merge_buf_size++;
	}

	// Remove the victime page from cache
	cached_keys[lru_page_idx] = 0;
	timestamps[lru_page_idx]  = 0xFFFFFFFF;
	page_reset_dirty(lru_page_idx);	
	num_free_sub_pages++;

	return lru_page_idx;
}

static UINT32 load_page(UINT32 const idx, pc_buf_type_t const type)
{
	// find a free page
	UINT32 free_page_idx = num_free_sub_pages == 0 ?
					evict_page() :
					mem_search_equ(cached_keys, sizeof(UINT32), 0);
	BUG_ON("free page not found after evicting", free_page_idx >= NUM_PC_SUB_PAGES);

	cached_keys[free_page_idx] = idx2key(idx, type);
	num_free_sub_pages--;

	UINT32 vspn = get_vspn(idx, type);
	if (vspn)
		fu_read_sub_page(vspn, PC_SUB_PAGE(free_page_idx));
	else
		mem_set_dram(PC_SUB_PAGE(free_page_idx), 0, BYTES_PER_SUB_PAGE);
	return free_page_idx;
}

/* ========================================================================= *
 * Public Functions 
 * ========================================================================= */

void page_cache_init(void)
{
	BUG_ON("Capacity too large", NUM_PC_SUB_PAGES > 255);

	UINT32 num_bytes = sizeof(UINT32) * NUM_PC_SUB_PAGES;
	mem_set_sram(cached_keys, 0, 		num_bytes);
	mem_set_sram(timestamps,  0xFFFFFFFF, 	num_bytes);
	mem_set_sram(dirty,	  0,		DIRTY_BYTES);
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
		last_key = key;
		last_page_idx = page_idx;
	}
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
	// update timestamp for LRU cache policy
	timestamps[page_idx] = ++current_timestamp;
	// handle timestamp overflow
	if (unlikely(current_timestamp == 0)) {
		mem_set_sram(timestamps, 0, sizeof(UINT32) * NUM_PC_SUB_PAGES);	
	}
	if (will_modify) page_set_dirty(page_idx);
	return PC_SUB_PAGE(page_idx);
}
