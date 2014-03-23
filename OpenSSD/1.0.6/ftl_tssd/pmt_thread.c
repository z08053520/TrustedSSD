#include "thread_handler_util.h"
#include "pmt_thread.h"
#include "pmt_cache.h"
#include "fla.h"
#include "signal.h"
#include "buffer.h"
#include "gc.h"
#include "scheduler.h"
#include "gtd.h"
#include "dram.h"

#define NULL_PMT_IDX		0xFFFFFFFF

#if OPTION_PERF_TUNING
UINT32 g_pmt_cache_flush_count = 0;
UINT32 g_pmt_cache_load_count = 0;
#endif

static thread_t *singleton_thread = NULL;

/*
 * Request queue
 * */
#define MAX_PMT_REQ_QUEUE_SIZE	(MAX_NUM_THREADS * SUB_PAGES_PER_PAGE)
static UINT32 pmt_req_queue[MAX_PMT_REQ_QUEUE_SIZE] = {0};
static UINT32 pmt_req_head = 0, pmt_req_tail = 0;
static UINT32 pmt_req_queue_size = 0;

static UINT32 pop_pmt_req()
{
	if (pmt_req_queue_size == 0) return NULL_PMT_IDX;

	UINT32 req_pmt_idx = pmt_req_queue[pmt_req_head];
	pmt_req_head = (pmt_req_head + 1) % MAX_PMT_REQ_QUEUE_SIZE;
	pmt_req_queue_size--;
	return req_pmt_idx;
}

void pmt_thread_request_enqueue(UINT32 const pmt_idx)
{
	ASSERT(pmt_req_queue_size < MAX_PMT_REQ_QUEUE_SIZE);

	/* wake up PMT thread */
	singleton_thread->state = THREAD_RUNNABLE;

	pmt_req_queue[pmt_req_tail] = pmt_idx;
	pmt_req_tail = (pmt_req_tail + 1) % MAX_PMT_REQ_QUEUE_SIZE;
	pmt_req_queue_size++;
}

/*
 * Handler
 * */

begin_thread_variables
	/* PMT loading info */
	UINT32	loading_pmt_idxes[NUM_BANKS];
	vsp_t	loading_pmt_vsps[NUM_BANKS];
	UINT8	loading_buf_ids[NUM_BANKS];
	/* PMT merge buffer */
	UINT8	flush_buf_ids[NUM_BANKS];
	/* PMT next request */
	UINT32	next_pmt_idx;
end_thread_variables

begin_thread_handler
/* PMT thread is designed as a event loop */
phase(ONE_PHASE) {
	/* uart_print("> queue size = %u", pmt_req_queue_size); */

	signals_t interesting_signals = 0;

	/* Check whether issued flash read cmds are complete */
	for_each_bank(bank_i) {
		UINT32 pmt_idx = var(loading_pmt_idxes)[bank_i];
		if (pmt_idx == NULL_PMT_IDX) continue;

		if (!fla_is_bank_complete(bank_i)) {
			signals_set(interesting_signals, SIG_BANK(bank_i));
			continue;
		}

		/* copy result to PMT buffer */
		UINT8	load_buf_id = var(loading_buf_ids)[bank_i];
		UINT32	load_buf = MANAGED_BUF(load_buf_id);
		UINT32	pmt_buf = pmt_cache_get(pmt_idx);
		ASSERT(pmt_buf != NULL);
		vsp_t	vsp = var(loading_pmt_vsps)[bank_i];
		UINT8	sp_offset = vsp.vspn % SUB_PAGES_PER_PAGE;
		UINT32	bytes_offset = sp_offset * BYTES_PER_SUB_PAGE;
		mem_copy(pmt_buf, load_buf + bytes_offset, BYTES_PER_SUB_PAGE);

		/* finishing */
		var(loading_pmt_idxes)[bank_i] = NULL_PMT_IDX;
		buffer_free(load_buf_id);
		pmt_cache_set_loaded(pmt_idx);

		signals_set(g_scheduler_signals, SIG_PMT_LOADED);
	}

	/* Check whether issued flash write cmds (flush) are complete */
	for_each_bank(bank_i) {
		UINT8 flush_buf_id = var(flush_buf_ids)[bank_i];
		if (flush_buf_id == NULL_BUF_ID) continue;

		if (!fla_is_bank_complete(bank_i)) {
			signals_set(interesting_signals, SIG_BANK(bank_i));
			continue;
		}

		/* finishing */
		buffer_free(flush_buf_id);
		var(flush_buf_ids)[bank_i] = NULL_BUF_ID;
	}

	/* Restore the last request or retrieve a new request */
	if (var(next_pmt_idx) == NULL_PMT_IDX)
		var(next_pmt_idx) = pop_pmt_req();
	/* Process request */
	while (var(next_pmt_idx) != NULL_PMT_IDX) {
		/* if the requested PMT page is loaded or being loaded,
		 * the we can safely ignore this duplicate request */
		if (pmt_cache_get(var(next_pmt_idx))) goto next_pmt_req;

		/* if cache is full, need to evict, even flush */
		if (pmt_cache_is_full()) {
			BOOL8 need_flush = pmt_cache_evict();
			/* evict a clean page */
			if (need_flush == FALSE) goto pmt_load;

			/* try to find a idle bank to flush */
			UINT8 flush_bank = fla_get_idle_bank();
			if (flush_bank >= NUM_BANKS) {
				signals_set(interesting_signals,
						SIG_ALL_BANKS);
				break;
			}
			signals_set(interesting_signals, SIG_BANK(flush_bank));

			/* need flush merge buffer for dirty pages */
			UINT8	flush_buf_id = buffer_allocate();
			var(flush_buf_ids)[flush_bank] = flush_buf_id;
			UINT32	flush_buf = MANAGED_BUF(flush_buf_id);
			UINT32	flush_pmt_idxes[SUB_PAGES_PER_PAGE];
			pmt_cache_flush(flush_buf, flush_pmt_idxes);

			/* issue flash write cmd */
			UINT32 flush_vpn = gc_allocate_new_vpn(flush_bank, TRUE);
			vp_t flush_vp = {
				.bank = flush_bank,
				.vpn = flush_vpn
			};
			fla_write_page(flush_vp, 0, SECTORS_PER_PAGE, flush_buf);
#if OPTION_PERF_TUNING
			g_pmt_cache_flush_count++;
#endif

			/* update GTD */
			vsp_t flush_vsp = {
				.bank = flush_bank,
				.vspn = flush_vpn * SUB_PAGES_PER_PAGE
			};
			for_each_subpage(sp_i) {
				UINT32 pmt_idx = flush_pmt_idxes[sp_i];
				gtd_set_vsp(pmt_idx, flush_vsp);
				flush_vsp.vspn++;
			}
		}
pmt_load:;
		vsp_t	load_vsp = gtd_get_vsp(var(next_pmt_idx));
		/* if this PMT page has never been written to flash */
		if (load_vsp.vspn == 0) {
			pmt_cache_put(var(next_pmt_idx));
			UINT32 pmt_buf = pmt_cache_get(var(next_pmt_idx));
			ASSERT(pmt_buf != NULL);

			mem_set_dram(pmt_buf, 0, BYTES_PER_SUB_PAGE);
			pmt_cache_set_loaded(var(next_pmt_idx));

			signals_set(g_scheduler_signals, SIG_PMT_LOADED);
			goto next_pmt_req;
		}

		UINT8 load_bank = load_vsp.bank;
		signals_set(interesting_signals, SIG_BANK(load_bank));
		if (!fla_is_bank_idle(load_bank)) break;

		/* reserve a place for the PMT page in cache */
		pmt_cache_put(var(next_pmt_idx));

		/* do flash read */
		UINT8	load_buf_id = buffer_allocate();
		UINT32	load_buf = MANAGED_BUF(load_buf_id);
		UINT32	load_vpn = load_vsp.vspn / SUB_PAGES_PER_PAGE;
		vp_t	load_vp = {.bank = load_bank, .vpn = load_vpn};
		UINT8	sp_offset = load_vsp.vspn % SUB_PAGES_PER_PAGE;
		UINT8	sect_offset = sp_offset * SECTORS_PER_SUB_PAGE;
		fla_read_page(load_vp, sect_offset, SECTORS_PER_SUB_PAGE,
				load_buf);
#if OPTION_PERF_TUNING
		g_pmt_cache_load_count++;
#endif

		var(loading_pmt_idxes)[load_bank] = var(next_pmt_idx);
		var(loading_pmt_vsps)[load_bank] = load_vsp;
		var(loading_buf_ids)[load_bank] = load_buf_id;
next_pmt_req:
		/* get next PMT request */
		var(next_pmt_idx) = pop_pmt_req();
	}

	/* uart_print("< queue size = %u", pmt_req_queue_size); */

	if (interesting_signals)
		sleep(interesting_signals);
	else
		/* need to be waken up */
		sleep(0);
}
end_thread_handler

/*
 * Initialiazation
 * */

static thread_handler_id_t registered_handler_id = NULL_THREAD_HANDLER_ID;

void pmt_thread_init(thread_t *t)
{
	/* pmt thread is a singleton; thus init can be only called once */
	ASSERT(registered_handler_id == NULL_THREAD_HANDLER_ID);
	registered_handler_id = thread_handler_register(get_thread_handler());

	singleton_thread = t;

	t->handler_id = registered_handler_id;

	/* init PMT loading info */
	for_each_bank(bank_i) {
		var(loading_pmt_idxes)[bank_i] = NULL_PMT_IDX;
	}
	/* init PMT merge buffer */
	for_each_bank(bank_i) {
		var(flush_buf_ids)[bank_i] = NULL_BUF_ID;
	}
	/* init next outstanding PMT request */
	var(next_pmt_idx) = NULL_PMT_IDX;

	init_thread_variables(thread_id(t));
}
