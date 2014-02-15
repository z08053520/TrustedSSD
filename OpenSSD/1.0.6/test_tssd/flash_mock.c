#include "flash_mock.h"
#include "test_util.h"
/*
 * The flash_mock module uses a FIFO caache to track the data of sectors in
 * flash. The diagram below shows how DRAM buffer is used as a FIFO cache.
 *
 *      Buffer
 *     |=======|	<-- old
 *     |       |
 *  ^  |       |
 *  |  |       |	<-- oldest
 *  |  |-------|	<-- next_buf_i
 *  |  |       |	<-- newest
 *  |  |       |
 *     |       |
 *     |=======|	<-- new
 *
 * */

/* ===========================================================================
 * Macros and variables
 * =========================================================================*/

#define	LBA_BUF_ADDR	_COPY_BUF(0)
#define VAL_BUF_ADDR	_COPY_BUF(NUM_COPY_BUFFERS / 2)
SETUP_BUF(lba, 		LBA_BUF_ADDR,	FLA_MOCK_BUF_SECTORS);
SETUP_BUF(val, 		VAL_BUF_ADDR, 	FLA_MOCK_BUF_SECTORS);

UINT32	next_buf_i;
#define LAST_BUF_I	(FLA_MOCK_BUF_SIZE - 1)
#define NULL_BUF_I	0xFFFFFFFF

/* ===========================================================================
 * Helper functions
 * =========================================================================*/

#define BUF_SERACH_STEP	(MU_MAX_BYTES / sizeof(UINT32))

static UINT32 find_latest(UINT32 const lba, UINT32 const from_buf_i,
				UINT32 const to_buf_i)
{
	UINT32 latest_buf_i;
	UINT32 buf_i = from_buf_i;
	UINT32 remain_buf_i = to_buf_i - from_buf_i + 1;
	UINT32 num_buf_i_one_step;
	while (remain_buf_i) {
		num_buf_i_one_step = remain_buf_i < BUF_SERACH_STEP ?
					remain_buf_i : BUF_SERACH_STEP;
		latest_buf_i = mem_search_equ_dram(LBA_BUF_ADDR + buf_i * sizeof(UINT32),
				sizeof(UINT32), num_buf_i_one_step, lba);
		if (latest_buf_i < num_buf_i_one_step) {
			latest_buf_i += buf_i;
			return latest_buf_i;
		}

		remain_buf_i -= num_buf_i_one_step;
		buf_i += num_buf_i_one_step;
	}
	return NULL_BUF_I;
}

/* ===========================================================================
 * Public functions
 * =========================================================================*/

void flash_mock_init()
{
	init_lba_buf(0xFFFFFFFF);
	init_val_buf(0);
	/* use buffer in a stack manner to work around limitations of
	 * built-in mem_search_equ functionality */
	next_buf_i = LAST_BUF_I;
}

void flash_mock_set(UINT32 const lba, UINT32 const val)
{
	set_lba(next_buf_i, lba);
	set_val(next_buf_i, val);

	if (next_buf_i == 0)
		next_buf_i = LAST_BUF_I;
	else
		next_buf_i--;
}

BOOL8 flash_mock_get(UINT32 const lba, UINT32 *val)
{
	UINT32 buf_i, from_buf_i, to_buf_i;

	/* search the lower part of buffer first */
	from_buf_i = next_buf_i + 1, to_buf_i = LAST_BUF_I;
	buf_i = find_latest(lba, from_buf_i, to_buf_i);

	/* if not found, then search the upper part of buffer */
	if (buf_i == NULL_BUF_I) {
		from_buf_i = 0, to_buf_i = next_buf_i;
		buf_i = find_latest(lba, from_buf_i, to_buf_i);
	}

	if (buf_i == NULL_BUF_I) return FALSE;
	*val = get_val(buf_i);
	return TRUE;

}
