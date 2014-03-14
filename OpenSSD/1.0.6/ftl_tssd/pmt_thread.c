#include "thread_handler_util.h"
#include "pmt_thread.h"
#include "pmt_cache.h"
#include "fla.h"
#include "signal.h"
#include "buffer.h"
#include "gc.h"
#include "scheduler.h"

static thread_t *singleton_thread = NULL;

/*
 * Request queue
 * */
#define PMT_REQ_QUEUE_SIZE	32
static UINT32 pmt_req_queue[PMT_REQ_QUEUE_SIZE] = {0};
static UINT32 pmt_req_head = 0, pmt_req_tail = 0;
static UINT32 pmt_req_size = 0;

static UINT32 pop_pmt_req()
{
	if (pmt_req_size == 0) return NULL_PMT_IDX;

	UINT32 req_pmt_idx = pmt_req_queue[pmt_req_head];
	pmt_req_head = (pmt_req_head + 1) % PMT_REQ_QUEUE_SIZE;
	pmt_req_size--;
	return req_pmt_idx;
}

BOOL8 pmt_thread_request_enqueue(UINT32 const pmt_idx)
{
	if (pmt_req_size == PMT_REQ_QUEUE_SIZE) return 1;

	if (singleton_thread->wakeup_signals == 0)
		signals_set(singleton_thread->wakeup_signals, SIG_ALL_BANKS);

	pmt_req_queue[pmt_req_tail] = pmt_idx;
	pmt_req_tail = (pmt_req_tail + 1) % PMT_REQ_QUEUE_SIZE;
	pmt_req_size++;
	return 0;
}

/*
 * Handler
 * */

begin_thread_stack
{
	/* PMT loading info */
	UINT32	loading_pmt_idxes[NUM_BANKS];
	vsp_t	loading_pmt_vsps[NUM_BANKS];
	UINT8	loading_buf_ids[NUM_BANKS];
	/* PMT merge buffer */
	UINT32	merged_pmt_idxes[SUB_PAGES_PER_PAGE];
	UINT8	num_merged_pmts;
	UINT8	merge_buf_ids[NUM_BANKS];
	UINT8	curr_merge_buf_id;
	/* PMT next request */
	UINT32	next_pmt_idx;
}
end_thread_stack

begin_thread_handler
{
/* PMT thread is designed as a event loop */
phase(ONE_PHASE) {
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
		var(loading_pmt_idexs)[bank_i] = NULL_PMT_IDX;
		buffer_free(load_buf_id);
		pmt_cache_set_reserved(pmt_idx, FALSE);

		signals_set(g_scheduler_signals, SIG_PMT_LOADED);
	}

	/* Check whether issued flash write cmds (flush) are complete */
	for_each_bank(bank_i) {
		UINT8 merge_buf_id = var(merge_buf_ids)[bank_i];
		if (merge_buf_id == NULL_BUF_ID) continue;

		if (!fla_is_bank_complete(bank_i)) {
			signals_set(interesting_signals, SIG_BANK(bank_i));
			continue;
		}

		/* finishing */
		buffer_free(merge_buf_id);
		var(merge_buf_ids)[bank_i] = NULL_BUF_ID;
	}

	/* Handle new PMT requests */
	UINT32 pmt_idx = var(next_pmt_idx);
	if (pmt_idx == NULL_PMT_IDX) pmt_idx = pop_pmt_req();
	while (pmt_idx != NULL_PMT_IDX) {
		/* if cache is full, need to evict */
		if (pmt_cache_is_full()) {
			UINT32	merge_buf = MANAGED_BUF(var(curr_merge_buf_id));

			/* merge buffer not full yet, don't need to flush */
			if (var(num_merged_pmts) < SUB_PAGES_PER_PAGE)
				goto evict_a_page;

			/* try to find a idle bank to flush */
			UINT8 flush_bank = fla_get_idle_bank();
			if (flush_bank >= NUM_BANKS) {
				signals_set(interesting_signals,
						SIG_ALL_BANKS);
				break;
			}
			signals_set(interesting_signals, SIG_BANK(flush_bank));

			/* issue flash write cmd */
			UINT32 flush_vpn = gc_allocate_new_vpn(flush_bank, TRUE);
			vp_t flush_vp = {
				.bank = flush_bank,
				.vpn = flush_vpn
			};
			fla_write_page(flush_vp, 0, SECTORS_PER_PAGE, merge_buf);

			/* update GTD */
			vsp_t flush_vsp = {
				.bank = flush_bank,
				.vspn = flush_vpn * SUB_PAGES_PER_PAGE
			}
			for_each_subpage(sp_i) {
				UINT32 pmt_idx = var(merged_pmt_idxes)[sp_i];
				gtd_set_vsp(pmt_idx, flush_vsp);
				flush_vsp.vspn++
			}

			var(merge_buf_ids)[flush_bank] = var(curr_merge_buf_id);
			var(num_merged_pmts) = 0;
			var(curr_merge_buf_id) = buffer_allocate();
evict_a_page:
			UINT32	evicted_pmt_idx;
			BOOL8	is_dirty;
			UINT32	evict_buf = merge_buf + var(num_merged_pmts)
							* BYTES_PER_SUB_PAGE;
			BOOL8	res = pmt_cache_evict(&evicted_pmt_idx, &is_dirty,
							evict_buf);
			/* FIXME: is this right? */
			ASSERT(res == 0);
			/* evict a clean entry, we are done */
			if (*is_dirty == FALSE) goto pmt_load;

			/* fill merge buffer with the newly evicted PMT page */
			var(merged_pmt_idxes)[var(num_merged_pmts)]
				= evicted_pmt_idx;
			var(num_merged_pmts)++;
		}
pmt_load:
		vsp_t	load_vsp = gtd_get_vsp(pmt_idx);
		UINT32	load_bank = load_vsp.bank;
		signals_set(interesting_signals, SIG_BANK(load_bank));
		if (!fla_is_bank_idle(load_bank)) break;

		/* do flash read */
		UINT8	load_buf_id = buffer_allocate();
		UINT32	load_buf = MANAGED_BUF(load_buf_id);
		UINT32	load_vpn = load_vsp.vspn / SUB_PAGES_PER_PAGE;
		vp_t	load_vp = {.bank = load_bank, .vp = load_vpn};
		UINT8	sp_offset = load_vsp.vspn % SUB_PAGES_PER_PAGE;
		UINT8	sect_offset = sp_offset * SECTORS_PER_SUB_PAGE;
		fla_read_page(load_vp, sect_offset, SECTORS_PER_SUB_PAGE,
				load_buf);

		var(loading_pmt_idxes)[load_bank] = pmt_idx;
		var(loading_pmt_vsps)[load_bank] = load_vsp;
		var(loading_buf_ids)[load_bank] = load_buf_id;

		signals_set(g_scheduler_signals, SIG_PMT_READY);

		/* get next PMT request */
		pmt_idx = pop_pmt_req();
	}
	/* remember this PMT request to try again next time */
	if (pmt_idx != NULL_PMT_IDX) var(next_pmt_idx) = pmt_idx;

	if (interesting_signals)
		sleep(interesting_signals);
	else
		/* need to be waken up */
		sleep(0);
}
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
	registered_handler_id = thread_handler_register(thread_handler);
	ASSERT(registered_handler_id != NULL_THREAD_HANDLER_ID);
	singleton_thread = t;

	t->handler = registered_handler_id;

	/* init PMT loading info */
	for_each_bank(bank_i) {
		var(loading_pmt_idxes)[bank_i] = NULL_PMT_IDX;
	}
	/* init PMT merge buffer */
	var(num_merged_pmts) = 0;
	var(current_merge_buf_id) = buffer_allocate();
	for_each_bank(bank_i) {
		var(merge_buf_ids)[bank_i] = NULL_BUF_ID;
	}
	/* init next outstanding PMT request */
	var(next_pmt_idx) = NULL_PMT_IDX;

	save_thread_variables(t);
}
