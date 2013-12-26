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

void ftl_test()
{
	uart_print("Start testing page cache...");

	init_idx_buf(0xFFFFFFFF);
	init_val_buf(0);

	srand(RAND_SEED);

	UINT32 i, j, pmt_index, pmt_page_val, pmt_buf;

	uart_print("Load empty pages in page cache and make sure they are empty");
	i = 0;
	while (i < MAX_ENTRIES) {
		pmt_index 	= rand() % PMT_SUB_PAGES;
		page_cache_load(pmt_index, &pmt_buf, PC_BUF_TYPE_PMT, FALSE); 	

		BUG_ON("empty PMT sub page is not empty!", 
		       is_buff_wrong(pmt_buf, 0, 0, SECTORS_PER_SUB_PAGE));
		i++;
	}

	uart_print("Randomly load pages to page cache, then write and verify its content");
	i = 0;
	while (i < MAX_ENTRIES) {
		// Verify data in buffer 
		pmt_index = rand() % PMT_SUB_PAGES;
		page_cache_load(pmt_index, &pmt_buf, PC_BUF_TYPE_PMT, TRUE); 	
		
		
		j 	  = mem_search_equ_dram(IDX_BUF, sizeof(UINT32), 
				     		MAX_ENTRIES, pmt_index);
		if (j >= MAX_ENTRIES) 
			pmt_page_val = 0;
		else 
			pmt_page_val = get_val(j);
		BUG_ON("The value in PMT sub page is not as expected!", 
		       is_buff_wrong(pmt_buf, pmt_page_val, 
			       	     0, SECTORS_PER_SUB_PAGE));
		
		// Modify data in buffer
		pmt_page_val = rand();
		mem_set_dram(pmt_buf, pmt_page_val, BYTES_PER_SUB_PAGE);
		
		if (j >= MAX_ENTRIES) {
			set_idx(i, pmt_index);
			set_val(i, pmt_page_val);
		}
		else
			set_val(j, pmt_page_val);

		i++;
	}
    	
	uart_print("Verify everything we write to page cache");
	i = 0;
	while (i < MAX_ENTRIES) {
		pmt_index    = get_idx(i);
		pmt_page_val = get_val(i);

		if (pmt_index != 0xFFFFFFFF) {
			page_cache_load(pmt_index, &pmt_buf, PC_BUF_TYPE_PMT, TRUE); 	
			BUG_ON("The value in PMT sub page is not as expected!", 
			       is_buff_wrong(pmt_buf, pmt_page_val, 
					     0, SECTORS_PER_SUB_PAGE));
		}

		i++;
	}
	
	uart_print("Page cache passed the unit test ^_^");
}
#endif
