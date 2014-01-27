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

typedef struct {
	TASK_PUBLIC_FIELDS
	page_key_t	key;
	UINT32		*buf;
	BOOL8		will_modify;
} load_page_task_t;

typedef enum {
	STATE_INIT,
	NUM_STATES
} state_t;

static UINT8	load_page_task_type;
static task_res_t load_page_handler (task_t*, task_context_t*);
static task_handler_t	handlers[NUM_STATES] = {
	load_page_handler,
};

static void load_page_task_register()
{
	BOOL8 res = task_engine_register_task_type(
			&load_page_task_type, handlers);
	BUG_ON("failed to register load page task", res);
}

static void load_page_task_init(task_t *_task, 
				page_key_t const key,
				UINT32 *buf,
				BOOL8 const will_modify)
{
	load_page_task_t *task = (load_page_task_t*) _task;

	task->type	= load_page_task_type;
	task->state	= STATE_INIT;

	task->key	= key;
	task->buf	= buf;
	task->will_modify = will_modify;
}

static task_res_t load_page_handler(task_t *_task, task_context_t *context)
{
	load_page_task_t *task = (load_page_task_t*) _task;

	task_res_t res = page_cache_load(task->key);
	if (res != TASK_CONTINUED) return res;

	page_cache_get(task->key, task->buf, task->will_modify);
	BUG_ON("buffer is NULL!", (*(task->buf)) == NULL);
	return TASK_FINISHED;
}

static void load_page(page_key_t const key, UINT32 *buf, BOOL8 const will_modify)
{
	task_t	*load_page_task = task_allocate();
	load_page_task_init(load_page_task, key, buf, will_modify);

	task_engine_submit(load_page_task);
	BOOL8 idle;
	do {
		idle = task_engine_run();
	} while(!idle);
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
	UINT32	i, j, old_i,
		num_indexes = NUM_PC_SUB_PAGES,
		max_index   = (1 << 20); 
	UINT32	buf, val;
	BOOL8	cached, will_modify;

	uart_print("Put items into page cache");
	i = 0;
	while (i < num_indexes) {
		key.type = rand() % 2;
		key.idx	 = rand() % max_index; 
		val	 = rand();
		
		/* Check whether the page is in cache */
		cached 	 = page_cache_has(key);
		old_i	 = mem_search_equ_dram(IDX_BUF, sizeof(UINT32), 
				     	       MAX_ENTRIES, key.as_uint);
		if (old_i < MAX_ENTRIES) {
			BUG_ON("should be in cache", !cached);
			continue;
		}
		BUG_ON("should not be in cache", cached);
			
		BUG_ON("should not be full", page_cache_is_full());

		page_cache_put(key, &buf, 0);
		cached 	 = page_cache_has(key);
		BUG_ON("should be in cache", !cached);

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

/* 	uart_print("Evict items in page cache"); */
/* 	i = 0; */
/* 	while (i < num_indexes / 2) { */
/* 		uart_printf("i = %u\r\n", i); */
		
/* 		BOOL8 need_flush = page_cache_evict(); */

/* 		key.as_uint = get_idx(i); */
/* 		if (i % 2 == 0) { */
/* 			/1* Odd items are in merge buffer *1/ */
/* 			BUG_ON("item i (even) should have been evicted", */ 
/* 				page_cache_has(key)); */
/* 		} */

/* 		if (i == 0) { */
/* 			i++; continue; */
/* 		} */

/* 		if (i % SUB_PAGES_PER_PAGE == 0) { */
/* 			BUG_ON("merge is full; should flush", */ 
/* 				!need_flush); */
			
/* 			UINT32	   merge_buf = COPY_BUF(0); */
/* 			page_key_t merge_keys[SUB_PAGES_PER_PAGE]; */
/* 			page_cache_flush(merge_buf, merge_keys); */	

/* 			/1* Check merge buffer *1/ */
/* 			j = 2 * (i - SUB_PAGES_PER_PAGE) + 1; */
/* 			UINT8	sp_i; */
/* 			for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++, j+=2) { */
/* 				key.as_uint = get_idx(j); */
/* 				val = get_val(j); */
				
/* 				BUG_ON("merge key not as expected", */ 
/* 					!page_key_equal(merge_keys[sp_i], */
/* 							key)); */
/* 				BUG_ON("merge bufffer not as expected", */
/* 					is_buff_wrong(merge_buf, val, */ 
/* 						sp_i * SECTORS_PER_SUB_PAGE, */ 
/* 						SECTORS_PER_SUB_PAGE)); */
/* 			} */
/* 		} */
/* 		else { */
/* 			BUG_ON("merge buffer is not full; don't need flush", need_flush); */
/* 		} */

/* 		i++; */
/* 	} */

	uart_print("Done");
}

static void loading_test()
{
	uart_print("Test loading functionality...");

	load_page_task_register();

	UINT32 num_entries = MAX_ENTRIES;
	/* UINT32 num_entries = 64; */

	/* restore the state set by previous test */
	page_cache_init();

	init_idx_buf(0xFFFFFFFF);
	init_val_buf(0);

	srand(RAND_SEED);

	UINT32 i, j;
	page_key_t key = {.type = PAGE_TYPE_PMT, .idx = 0};
	UINT32 page_val, buf;

	uart_print("Load empty pages in page cache and make sure they are empty");
	i = 0;
	while (i < num_entries) {
		/* uart_printf("i = %u\r\n", i); */

		key.idx = rand() % PMT_SUB_PAGES;
		load_page(key, &buf, FALSE); 	

		BUG_ON("empty PMT sub page should be initialized to zeros!", 
		       is_buff_wrong(buf, 0, 0, SECTORS_PER_SUB_PAGE));
		i++;
	}

	uart_print("Randomly load pages to page cache, then write and verify its content");
	i = 0;
	while (i < num_entries) {
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
		
		/* uart_printf("i = %u, key = %u, val = %u\r\n", */ 
		/* 		i, key.as_uint, page_val); */
		
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
	while (i < num_entries) {
		key.idx  = get_idx(i);
		page_val = get_val(i);

		/* uart_printf("i = %u, key = %u, val = %u\r\n", */ 
		/* 	    i, key.as_uint, page_val); */

		load_page(key, &buf, FALSE); 	
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
