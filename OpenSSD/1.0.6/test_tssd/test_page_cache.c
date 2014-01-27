/* ===========================================================================
 * Unit test for page cache 
 * =========================================================================*/

#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "test_util.h"
#include "page_cache.h"
#include <stdlib.h>

/* ========================================================================= *
 *  Macros 
 * ========================================================================= */
#define IDX_BUF		TEMP_BUF_ADDR
#define VAL_BUF		HIL_BUF_ADDR

SETUP_BUF(idx,		IDX_BUF,	SECTORS_PER_PAGE);
SETUP_BUF(val,		VAL_BUF,	SECTORS_PER_PAGE);

#define MAX_ENTRIES	(BYTES_PER_PAGE / sizeof(UINT32))

#define RAND_SEED	123456

/* ========================================================================= *
 *  Helper Functions 
 * ========================================================================= */

static void load_page(page_key_t const key, UINT32 *buf, BOOL8 const will_modify)
{
	BOOL8		idle = FALSE;
	task_res_t	res = page_cache_load(key);
	while (res != TASK_CONTINUED) {
		BUG_ON("task engine is idle, but page cache haven't loaded", idle);
		idle = task_engine_run();
		res = page_cache_load(key);
	}
	page_cache_get(key, buf, will_modify);
}

/* ========================================================================= *
 *  Tests 
 * ========================================================================= */
static void caching_test()
{
	uart_print("Test caching functionality...");

	init_idx_buf(0xFFFFFFFF);
	init_val_buf(0);

	page_key_t key;
	UINT32	i, j, 
		old_i,
		num_indexes = NUM_PC_SUB_PAGES,
		max_index   = (1 << 20); 
	UINT32	buf, val;
	BOOL8	cached, will_modify;

	uart_print("Put items into page cache");
	i = 0;
	while (i < num_indexes) {
		key.type = rand() % 2;
		key.idx	 = rand() % max_index; 
		cached 	 = page_cache_has(key);

		old_i	 = mem_search_equ_dram(IDX_BUF, sizeof(UINT32), 
				     	      MAX_ENTRIES, key.as_uint);
		if (old_i < MAX_ENTRIES) {
			BUG_ON("should be in cache", !cached);

			page_cache_get(key, &buf, FALSE);
			BUG_ON("buffer is null!", buf == NULL);
			continue;
		}
		BUG_ON("should not be in cache", cached);
			
		BUG_ON("should not be full", page_cache_is_full());

		page_cache_put(key, &buf, 0);
		cached 	 = page_cache_has(key);
		BUG_ON("should be in cache", !cached);

		UINT32	val = rand();
		mem_set_dram(buf, val, BYTES_PER_SUB_PAGE);

		set_idx(i, key.as_uint);
		set_val(i, val);

		i++;
	}
	BUG_ON("should be full", !page_cache_is_full());

	uart_print("Verify items in page cache");
	i = 0;
	while (i < num_indexes) {
		key.as_uint = get_idx(i);
		val = get_val(i);

		will_modify = (i % 2 == 1);
		page_cache_get(key, &buf, will_modify);
		BUG_ON("buffer is null!", buf == NULL);
		BUG_ON("the content of buffer is not as expected", 
			is_buff_wrong(buf, val, 0, SECTORS_PER_SUB_PAGE));

		i++;
	}

	uart_print("Evict items in page cache");
	i = 0;
	while (1) {
		BOOL8 need_flush = page_cache_evict();

		key.as_uint = get_idx(i);
		BUG_ON("item i should have been evicted", 
			page_cache_has(key));

		i++;	
		if (i >= num_indexes) break;	

		if (i % (2 * SUB_PAGES_PER_PAGE) == 0) {
			BUG_ON("merge is full; should flush", 
				!need_flush);
			
			UINT32	   merge_buf = COPY_BUF(0);
			page_key_t merge_keys[SUB_PAGES_PER_PAGE];
			page_cache_flush(merge_buf, merge_keys);	

			/* Check merge buffer */
			j = i - 2 * SUB_PAGES_PER_PAGE + 1;
			UINT8	sp_i;
			for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
				key.as_uint = get_idx(j);
				val = get_val(j);
				
				BUG_ON("merge key not as expected", 
					!page_key_equal(merge_keys[sp_i],
							key));
				BUG_ON("merge bufffer not as expected",
					is_buff_wrong(merge_buf, val, 
						sp_i * SECTORS_PER_SUB_PAGE, 
						SECTORS_PER_SUB_PAGE));
			}
		}
		else {
			BUG_ON("merge is not full; don't need flush", 
				need_flush);
		}
	}

	uart_print("Done");
}

static void loading_test()
{
	uart_print("Test loading functionality...");

	init_idx_buf(0xFFFFFFFF);
	init_val_buf(0);

	srand(RAND_SEED);

	UINT32 i, j;
	page_key_t key = {.type = PAGE_TYPE_PMT, .idx = 0};
	UINT32 page_val, buf;

	uart_print("Load empty pages in page cache and make sure they are empty");
	i = 0;
	while (i < MAX_ENTRIES) {
		key.idx = rand() % PMT_SUB_PAGES;
		load_page(key, &buf, FALSE); 	

		BUG_ON("empty PMT sub page should be initialized to zeros!", 
		       is_buff_wrong(buf, 0, 0, SECTORS_PER_SUB_PAGE));
		i++;
	}

	uart_print("Randomly load pages to page cache, then write and verify its content");
	i = 0;
	while (i < MAX_ENTRIES) {
		// Verify data in buffer 
		key.idx = rand() % PMT_SUB_PAGES;
		load_page(key, &buf, TRUE); 	
		
		j 	  = mem_search_equ_dram(IDX_BUF, sizeof(UINT32), 
				     		MAX_ENTRIES, key.idx);
		page_val = (j >= MAX_ENTRIES ? 0 : get_val(j));
		BUG_ON("The value in PMT sub page is not as expected!", 
		       is_buff_wrong(buf, page_val, 
			       	     0, SECTORS_PER_SUB_PAGE));
		
		// Modify data in buffer
		page_val = rand();
		mem_set_dram(buf, page_val, BYTES_PER_SUB_PAGE);
		
		if (j >= MAX_ENTRIES) {
			set_idx(i, key.idx);
			set_val(i, page_val);
			i++;
		}
		else
			set_val(j, page_val);
	}
    	
	uart_print("Verify everything we write to page cache");
	i = 0;
	while (i < MAX_ENTRIES) {
		key.idx  = get_idx(i);
		page_val = get_val(i);

		load_page(key, &buf, TRUE); 	
		BUG_ON("The value in PMT sub page is not as expected!", 
		       is_buff_wrong(buf, page_val, 
				     0, SECTORS_PER_SUB_PAGE));

		i++;
	}
	uart_print("Done");	
}

/* ========================================================================= *
 *  Entry Point 
 * ========================================================================= */
void ftl_test()
{
	uart_print("Start testing page cache...");

	caching_test();
	loading_test();

	uart_print("Page cache passed the unit test ^_^");
}

#endif
