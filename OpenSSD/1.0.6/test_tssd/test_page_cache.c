/* ===========================================================================
 * Unit test for page cache 
 * =========================================================================*/

#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "test_util.h"
#include "page_cache.h"
#include <stdlib.h>

#define IDX_BUF		TEMP_BUF_ADDR
#define VAL_BUF		HIL_BUF_ADDR

SETUP_BUF(idx,		IDX_BUF,	SECTORS_PER_PAGE);
SETUP_BUF(val,		VAL_BUF,	SECTORS_PER_PAGE);

#define MAX_ENTRIES	(BYTES_PER_PAGE / sizeof(UINT32))

#define RAND_SEED	123456

static void load_page(page_key_t const key, UINT32 *buf, BOOL8 const will_modify)
{
	BOOL8		idle = FALSE;
	task_res_t	res = page_cache_load(key);
	while (res != TASK_CONTINUED) {
		BUG_ON("task engine is idle, but page cache haven't load", idle);
		idle = task_engine_run();
		res = page_cache_load(key);
	}
	page_cache_get(key, buf, will_modify);
}

void ftl_test()
{
	uart_print("Start testing page cache...");

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
	
	uart_print("Page cache passed the unit test ^_^");
}
#endif
