#include "ftl_write_task.h"
#include "dram.h"
#include "pmt.h"
#include "write_buffer.h"

/* ===========================================================================
 *  Macros, types and global variables
 * =========================================================================*/

typedef enum {
	STATE_PREPARATION,
	STATE_MAPPING,
	STATE_FLASH_READ,
	STATE_FLASH_WRITE,
	STATE_FINISH,
	NUM_STATES
} state_t;

typedef struct {
	TASK_PUBLIC_FIELDS
	UINT32		seq_id;
	UINT32		lpn;
	UINT8		offset;
	UINT8		num_sectors;
	/* write buffer */
	UINT8		wb_sp_cmd_issued;
	UINT8		wb_sp_cmd_done;
	vp_t		wb_vp;
	UINT32		wb_buf;
	sectors_mask_t 	wb_valid_sectors;
	UINT32		wb_lspn[SUB_PAGES_PER_PAGE];
	vp_t		wb_old_vp[SUB_PAGES_PER_PAGE];
} ftl_write_task_t;

#define task_buf_id(task)	((task->seq_id) % NUM_SATA_WR_BUFFERS)

static UINT8		ftl_write_task_type;

static task_res_t preparation_state_handler	(task_t*, banks_mask_t*);
static task_res_t mapping_state_handler		(task_t*, banks_mask_t*);
static task_res_t flash_read_state_handler	(task_t*, banks_mask_t*);
static task_res_t flash_write_state_handler	(task_t*, banks_mask_t*);
static task_res_t finish_state_handler		(task_t*, banks_mask_t*);

static task_handler_t handlers[NUM_STATES] = {
	preparation_state_handler,
	mapping_state_handler,
	flash_read_state_handler,
	flash_write_state_handler,
	finish_state_handler
};

UINT32	g_num_ftl_write_tasks_submitted = 0;
UINT32	g_num_ftl_write_tasks_finished  = 0;

#define lpn2lspn(lpn)		(lpn * SUB_PAGES_PER_PAGE)

/* ===========================================================================
 *  Task Handlers
 * =========================================================================*/

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

static task_res_t preparation_state_handler(task_t* task, 
					    banks_mask_t* idle_banks)
{
	/* Assign an unique id to each task */
	write_task->seq_id	= g_num_ftl_write_tasks_submitted++;

	UINT32 write_buf_id	= task_buf_id(task);
	// FIXME: this waiting may not be necessary
	// Wait for SATA transfer completion
#if OPTION_FTL_TEST == 0
	if (write_buf_id == GETREG(SATA_WBUF_PTR)) return TASK_BLOCKED;
#endif

	write_task->wb_buf	= NULL;
	/* Insert and merge into write buffer */
	if (task->num_sectors < SECTORS_PER_PAGE) {
		if (write_buffer_is_full()) {
			if (*idle_banks == 0) return TASK_BLOCKED;

			UINT8 idle_bank  = auto_idle_bank(idle_banks)
			task->wb_vp.bank = idle_bank;
			task->wb_vp.vpn  = gc_allocate_new_vpn(idle_bank);
			task->wb_buf	 = FTL_WR_BUF(idle_bank);	

			write_buffer_flush(task->wb_buf, task->wb_lspn, 
					   &task->wb_valid_secotrs);
		}

		write_buffer_put(task->lpn, task->offset, task->num_sectors, 
				 SATA_WR_BUF_PTR(write_buf_id));
	}
	/* Bypass write buffer and use SATA buffer directly */
	else {
		if (*idle_banks == 0) return TASK_BLOCKED;

		UINT8 idle_bank  = auto_idle_bank(idle_banks)
		task->wb_vp.bank = idle_bank;
		task->wb_vp.vpn  = gc_allocate_new_vpn(idle_bank);
		task->wb_buf	 = SATA_WR_BUF_PTR(write_buf_id);
		task->wb_valid_secotrs = FULL_MASK;

		UINT32 lspn_base = lpn2lspn(task->lpn);
		UINT8 sp_i;
		for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++)
			task->lspn[sp_i] = lspn_base + sp_i;

		write_buffer_drop(task->lpn);
	}

	if (task->wb_buf == NULL) {
		task->state = STATE_FINISH;
		return TASK_CONTINUED;
	}

	task->state = STATE_MAPPING;
	return TASK_CONTINUED;
}

static task_res_t mapping_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	UINT8 sp_i;
	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT32 lspn = task->lspn[sp_i];
		pmt_fetch(lspn, & task->old_vp[sp_i]);		
		pmt_update(lspn, task->vp);
	}
	
	task->waiting_banks 	= 0;
	task->state 		= STATE_FLASH_READ;
	task->wb_sp_cmd_issued 	= 0;
	task->wb_sp_cmd_done 	= 0;
	return TASK_CONTINUED;
}

#define begin_subpage(mask)	(begin_sector(mask) / SECTORS_PER_SUB_PAGE)
#define end_subpage(mask)	(end_sector(mask) / SECTORS_PER_SUB_PAGE)

#define mask_is_set(mask, i)		(((mask) >> (i)) & 1)
#define mask_set(mask, i)		((mask) |= (1 << (i)))
#define mask_clear(mask, i)		((mask) &= ~(1 << (i)))

#define banks_has(banks, bank)		mask_is_set(banks, bank)
#define banks_add(banks, bank)		mask_set(banks, bank)
#define banks_del(banks, bank)		mask_clear(banks, bank)

#define FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler, sp_mask)	\
	UINT8 i = 0;							\
	while (i < SECTORS_PER_SUB_PAGE) {				\
		/* find the first missing sector */			\
		while (i < SECTORS_PER_SUB_PAGE &&			\
		       mask_is_set(sp_mask, i)) i++;			\
		if (i == SECTORS_PER_SUB_PAGE) break;			\
		UINT8 begin_i = i++;					\
		/* find the last missing sector */			\
		while (i < SECTORS_PER_SUB_PAGE &&			\
		       !mask_is_set(sp_mask, i)) i++;			\
		UINT8 end_i   = i++;					\
		/* evoke segment handler */				\
		(*segment_handler)(begin_i, end_i);			\
	}

static task_res_t flash_read_state_handler(task_t* task, 
					   banks_mask_t* idle_banks)
{
	BUG_ON("no valid sectors in write buffer", task->wb_valid_sectors == 0);
	UINT8	begin_sp = begin_subpage(task->wb_valid_sectors),
		end_sp	 = end_subpage(task->wb_valid_sectors);
	UINT8 	sp_i;
	for (sp_i = begin_sp; sp_i < end_sp; sp_i++) {
		if (mask_is_set(task->wb_sp_cmd_done, sp_i)) continue;	
		
		UINT8	sp_mask = (task->wb_valid_sectors >> 
					(SECTORS_PER_SUB_PAGE * sp_i));

		/* all sectors are valid; skip this sub-page*/
		if (sp_mask == 0xFF) {
			mask_set(task->wb_sp_cmd_done, sp_i);
			continue;
		}

		/* fill "hole" with 0xFF..FF */
		if (sp_mask == 0x00) {
			mem_set_dram(task->wb_buf + sp_i * BYTES_PER_SUB_PAGE, 
				     0xFFFFFFFF, BYTES_PER_SUB_PAGE);	
			mask_set(task->wb_sp_cmd_done, sp_i);
			continue;
		}

		UINT8 bank = task->wb_old_vp[sp_i].bank	
		/* issue flash cmd to fill the paritial sub-page */
		if (!mask_is_set(task->wb_sp_cmd_issued, sp_i)) {
			vp_t vp    = task->wb_old_vp[sp_i].vp;

			/* if this sub-page is never written before */
			if (vp.vpn == 0) {
				UINT32 sp_target_buf = task->wb_buf + BYTES_PER_SUB_PAGE * sp_i;
				void (*segment_handler) (UINT8 const, UINT8 const) = 
					/* fill missing sectors from begin_i to end_i */
					lambda (void, (UINT8 const begin_i, UINT8 const end_i) {
						mem_set_dram(sp_target_buf + begin_i * BYTES_PER_SECTOR,
							     0xFFFFFFFF, (end_i - begin_i) * BYTES_PER_SECTOR);
					});
				FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler, sp_mask);

				mask_set(task->wb_sp_cmd_done, sp_i);
				continue;
			}
		
			/* bank is needed to issue flash cmd */
			banks_add(task->waiting_banks, bank);

			/* skip if bank is not available */
			if (!banks_has(*idle_banks, bank)) continue;

			fu_read_sub_page_async(vp, FTL_RD_BUF(bank));
			mask_set(task->wb_sp_cmd_issued, sp_i);
		}
		/* if flash cmd is done */
		else if (banks_has(*idle_banks, bank)) {
			UINT32 	sp_target_buf = task->wb_buf + sp_i * BYTES_PER_SUB_PAGE,
				sp_src_buff   = FTL_RD_BUF(bank)+ sp_i * BYTES_PER_SUB_PAGE; 
			void (*segment_handler) (UINT8 const, UINT8 const) = 
				lambda (void, (UINT8 const begin_i, const UINT8 end_i) {
					mem_copy(sp_target_buf + begin_i * BYTES_PER_SECTOR,
						 sp_src_buf    + begin_i * BYTES_PER_SECTOR,
						 (end_i - begin_i) * BYTES_PER_SECTOR);
				});
			FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler, sp_mask);

			banks_del(task->waiting_banks, bank);
			mask_set(task->wb_sp_cmd_done, sp_i);
		}
	}

	/* if some sub-pages are not filled, pause this task */
	if (task->waiting_banks) return TASK_PAUSED;

	task->state = STATE_FLASH_WRITE;
	return TASK_CONTINUED;
}

static task_res_t flash_write_state_handler(task_t* task, 
					    banks_mask_t* idle_banks)
{
	if (task->waiting_banks == 0) {
		UINT8	bank		= task->wb_vp.bank;
		if (!banks_has(*idle_banks, bank)) return TASK_PAUSED;

		UINT32	vpn 		= task->wb_vp.vpn;	
			offset 	   	= begin_sector(task->wb_valid_sectors),
			num_sectors 	= end_sector(victim_buf_mask) - offset;
		nand_page_ptprogram(bank, 
				    vpn / PAGES_PER_VBLK, 
				    vpn % PAGES_PER_VBLK,
				    offset, num_sectors,
				    task->wb_buf);

		banks_add(task->waiting_banks, bank);
		banks_del(*idle_banks, bank);
		return TASK_PAUSED;
	}
	else if (banks_has(*idle_banks, bank)) {
		task->state = STATE_FINISH;
		return TASK_CONTINUED;
	}
}

static task_res_t finish_state_handler	(task_t* task, 
					 banks_mask_t* idle_banks)
{
	/* if all tasks previous to this one is completed, then we can 
	 * safely inform SATA buffer manager to update pointer */
	if (g_num_ftl_write_tasks_finished == task->seq_id) {
		UINT32 next_write_buf_id = (task_buf_id(task) + 1) 
					 % NUM_SATA_WRITE_BUFFERS;
		SETREG(BM_STACK_WRSET, next_write_buf_id);
		SETREG(BM_STACK_RESET, 0x01);
	}
	g_num_ftl_write_tasks_finished++;
	BUG_ON("impossible counter", g_num_ftl_write_tasks_finished
			   	   > g_num_ftl_write_tasks_submitted);
	return TASK_FINISHED;
}

/* ===========================================================================
 *  Public Interface
 * =========================================================================*/

void ftl_write_task_register()
{
	BUG_ON("ftl write task structure is too large to fit into "
	       "general task structure", sizeof(ftl_write_task_t) > sizeof(task_t));
	
	g_num_ftl_write_tasks_submitted = 0;
	g_num_ftl_write_tasks_finished  = 0;

	BOOL8 res = task_engine_register_task_type(
			&ftl_write_task_type, handlers);
	BUG_ON("failed to register FTL write task", res);
}

void ftl_write_task_init(task_t *task, UINT32 const lpn, 
		   	UINT8 const offset, UINT8 const num_sectors)
{
	ftl_write_task_t *write_task = (ftl_write_task_t*) task;

	write_task->type	= ftl_write_task_type;
	write_task->state	= STATE_PREPARATION;

	write_task->lpn 	= lpn;
	write_task->offset	= offset;
	write_task->num_sectors	= num_sectors;
}
