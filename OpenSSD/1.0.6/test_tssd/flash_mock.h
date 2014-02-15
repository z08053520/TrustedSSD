#ifndef __FLASH_MOCK_H
#define __FLASH_MOCK_H

/*
 * The flash_mock module tracks the state of flash chips resulted from
 * some read or write operations. It is intended to be used by unit tests to
 * verify whether the `real` flash contains data as expected.
 *
 * flash_mock only provides best-effort guarantee on tracking the state of
 * flash, as the amount of DRAM that can be used by flash_mock is limited.
 * */

#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"

#define FLA_MOCK_BUF_SECTORS	(NUM_COPY_BUFFERS / 2 * SECTORS_PER_PAGE)
#define FLA_MOCK_BUF_BYTES	(FLA_MOCK_BUF_SECTORS * BYTES_PER_SECTOR)
#define FLA_MOCK_BUF_SIZE	(FLA_MOCK_BUF_BYTES / sizeof(UINT32))

void flash_mock_init();
void flash_mock_set(UINT32 const lba, UINT32 const val);
BOOL8 flash_mock_get(UINT32 const lba, UINT32 *val);

#endif
#endif
