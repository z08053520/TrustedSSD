/* ===========================================================================
 * Unit test for flash util 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "task_engine.h"
#include "flash_util.h"
#include "dram.h"
#include "gc.h"
#include "test_util.h"
#include <stdlib.h>

/* ===========================================================================
 * Macros and Variables 
 * =========================================================================*/
#define RAND_SEED	123456

#define VSP_BUF_ADDR		TEMP_BUF_ADDR
#define MAX_NUM_SP		(BYTES_PER_PAGE / sizeof(UINT32))
SETUP_BUF(vsp, 			VSP_BUF_ADDR, 		SECTORS_PER_PAGE);

#define NUM_WRITE_PAGE_TASKS		(MAX_NUM_SP / SUB_PAGES_PER_PAGE)
#define NUM_VERIFY_SUB_PAGE_TASKS	(NUM_WRITE_TASKS * SUB_PAGES_PER_PAGE)

typedef enum {
	STATE_INIT,
	STATE_SUBMIT,
	STATE_WAIT,
	NUM_STATES
} state_t; 

/* ===========================================================================
 *  Write Page Task
 * =========================================================================*/

typedef struct {
	TASK_PUBLIC_FIELDS
	UINT32		seq_id;
	vp_t		vp;
} write_page_task_t;

static UINT8	write_page_task_type;

static task_res_t wp_init_handler	(task_t*, banks_mask_t*);
static task_res_t wp_submit_handler	(task_t*, banks_mask_t*);
static task_res_t wp_wait_handler	(task_t*, banks_mask_t*);

static task_handler_t write_handlers[NUM_STATES] = {
	wp_init_handler,
	wp_submit_handler,
	wp_wait_handler
};

static void write_page_task_register()
{
	BOOL8 res = task_engine_register_task_type(
			&write_page_task_type, write_handlers);
	BUG_ON("failed to register FTL read task", res);
}

static void write_page_task_init(task_t *_task, UINT32 const seq_id)
{
	write_page_task_t *task = (write_page_task_t*) _task;

	task->type	= write_page_task_type;
	task->state	= STATE_INIT;
	task->seq_id	= seq_id;
}

static UINT8	auto_idle_bank(banks_mask_t* idle_banks)
{
	static UINT8 bank_i = NUM_BANKS - 1;

	UINT8 i;
	for (i = 0; i < NUM_BANKS; i++) {
		bank_i = (bank_i + 1) % NUM_BANKS;
		if (banks_has(*idle_banks, bank_i)) return bank_i;
	}
	return NUM_BANKS;
}

static task_res_t wp_init_handler	(task_t* _task, banks_mask_t* idle_banks)
{
	if (*idle_banks == 0) return TASK_BLOCKED;
	
	write_page_task_t *task = (write_page_task_t*) _task;
	
	UINT8	idle_bank = auto_idle_bank(idle_banks);
	UINT32	vpn	  = gc_allocate_new_vpn(idle_bank);
	task->vp.bank	  = idle_bank;
	task->vp.vpn	  = vpn;

	task->state	  = STATE_SUBMIT;
	return TASK_CONTINUED;
}

static task_res_t wp_submit_handler	(task_t* _task, banks_mask_t* idle_banks)
{
	write_page_task_t *task = (write_page_task_t*) _task;

	UINT8	bank		= task->vp.bank;
	UINT32	vspn_base	= task->vp.vpn * SUB_PAGES_PER_PAGE;
	UINT32	wr_buf		= FTL_WR_BUF(bank);

	UITN32	vspn;
	UINT8 	sp_i;
	UINT32 	val;
	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		vspn = vspn_base + sp_i;
		val  = vspn;

		vsp_t	vsp = {.bank = bank, .vspn = vspn};
		vsp_or_int vsp_int = {.as_vsp = vsp};
		set_vsp(task->seq_id + sp_i, vsp_int.as_int);

		mem_set_dram(wr_buf + sp_i * BYTES_PER_SUB_PAGE,
			     val, BYTES_PER_SUB_PAGE);
	}
	fu_write_page(task->vp, wr_buf);

	banks_del(*idle_banks, bank);

	task->state		= STATE_WAIT;
	task->waiting_banks	= (1 << bank);
	return TASK_PAUSED;
}

static task_res_t wp_wait_handler	(task_t* _task, banks_mask_t* idle_banks)
{
	write_page_task_t *task = (write_page_task_t*) _task;

	if (!banks_has(*idle_banks, task->vp.bank)) return TASK_PAUSED;

	return TASK_FINISHED;
}

/* ===========================================================================
 *  Verify Sub-Page Task 
 * =========================================================================*/

typedef struct {
	TASK_PUBLIC_FIELDS
	UINT32		seq_id;
	vsp_t		vsp;
} verify_subpage_task_t;

static UINT8	verify_subpage_task_type;

static task_res_t vsp_init_handler	(task_t*, banks_mask_t*);
static task_res_t vsp_submit_handler	(task_t*, banks_mask_t*);
static task_res_t vsp_wait_handler	(task_t*, banks_mask_t*);

static task_handler_t verify_handlers[NUM_STATES] = {
	vsp_init_handler,
	vsp_submit_handler,
	vsp_wait_handler
};

static void verify_subpage_task_register()
{
	BOOL8 res = task_engine_register_task_type(
			&verify_subpage_task_type, verify_handlers);
	BUG_ON("failed to register FTL read task", res);
}

static void verify_subpage_task_init(task_t *_task, UINT32 const seq_id)
{
	verify_subpage_task_t *task = (verify_subpage_task_t*) _task;

	task->type	= verify_subpage_task_type;
	task->state	= STATE_INIT;
	task->seq_id	= seq_id;

	vsp_or_int	int2vsp = {.as_int = get_vsp(task->seq_id)};
	task->vsp	= int2vsp.as_vsp;
	
	task->waiting_banks = (1 << task->vsp.bank);
}

static task_res_t vsp_init_handler	(task_t* _task, banks_mask_t* idle_banks)
{
	verify_subpage_task_t *task = (verify_subpage_task_t*) _task;
	
	if (!banks_has(*idle_banks, task->vsp.bank)) return TASK_PAUSED;

	task->state	  = STATE_SUBMIT;
	return TASK_CONTINUED;
}

static task_res_t vsp_submit_handler	(task_t* _task, banks_mask_t* idle_banks)
{
	verify_subpage_task_t *task = (verify_subpage_task_t*) _task;

	UINT8	bank = task->vsp.bank;
	fu_read_sub_page(task->vsp, FTL_RD_BUF(bank), FU_ASYNC);

	banks_del(*idle_banks, bank);

	task->state		= STATE_WAIT;
	return TASK_PAUSED;
}

static task_res_t vsp_wait_handler	(task_t* _task, banks_mask_t* idle_banks)
{
	verify_subpage_task_t *task = (verify_subpage_task_t*) _task;

	if (!banks_has(*idle_banks, task->vp.bank)) return TASK_PAUSED;

	UINT8	bank	      = vsp.bank;
	UINT8	sector_offset = vsp.vspn % SUB_PAGES_PER_PAGE * SECTORS_PER_SUB_PAGE;
	UINT32	val	      = vsp.vspn;
	UINT8	wrong 	      = is_buff_wrong(FTL_RD_BUF(bank), 
					      val, 
					      sector_offset, 
					      SECTORS_PER_SUB_PAGE);
	BUG_ON("data read from flash is not the same as data written to flash", wrong);
	return TASK_FINISHED;
}

/* ===========================================================================
 *  Test 
 * =========================================================================*/

void ftl_test()
{
	uart_print("Start testing task engine");

	init_task_handlers();

	UINT32 	task_i;	
	BOOL8	is_idle;

	/* Do write page tasks */
	uart_print("Do write page task...");
	for (task_i = 0; task_i < NUM_WRITE_PAGE_TASKS; task_i++) {
		while (!task_engine_can_allocate(1))
			task_engine_run();
		
		task_t *task = task_allocate();	
		BUG_ON("allocation task failed", task == NULL);
		write_page_task_init(task, task_id);		
		task_engine_submit(task);
		task_engine_run();
	}
	/* Make sure all write page task are completed */
	do {
		is_idle = task_engine_run();
	} while(!is_idle);
	uart_print("Done");

	/* Do verification sub-page tasks */
	uart_print("Do verification sub-page task...");
	for (task_i = 0; task_i < NUM_VERIFY_SUB_PAGE_TASKS; task_i++) {
		while (!task_engine_can_allocate(1))
			task_engine_run();
		
		task_t *task = task_allocate();	
		BUG_ON("allocation task failed", task == NULL);
		verify_subpage_task_init(task, task_id);	
		task_engine_submit(task);
		task_engine_run();
	}
	/* Make sure all verify sub page task are completed */
	do {
		is_idle = task_engine_run();
	} while(!is_idle);
	uart_print("Done");

	uart_print("Task engine passed the unit test");
}

#endif
