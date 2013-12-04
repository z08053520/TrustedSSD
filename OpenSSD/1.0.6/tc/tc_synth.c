// Copyright 2011 INDILINX Co., Ltd.
//
// This file is part of Jasmine.
//
// Jasmine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Jasmine is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Jasmine. See the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.

#include "jasmine.h"

#if FTL_TEST == 1
const UINT32 io_limit = NUM_LSECTORS;

const UINT32 NUM_PSECTORS_4KB = (4 * 1024) / 512;
const UINT32 NUM_PSECTORS_8KB = (NUM_PSECTORS_4KB << 1);
const UINT32 NUM_PSECTORS_16KB = (NUM_PSECTORS_8KB << 1);
const UINT32 NUM_PSECTORS_32KB = (NUM_PSECTORS_16KB << 1);
const UINT32 NUM_PSECTORS_128KB = (128 * 1024) / 512;

static void tc_write_seq(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size);
static void tc_write_rand(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size);

static volatile UINT32 end;

void tc_synth(void)
{
    /* ftl_test_write(0, 4); */
    /* ftl_test_write(4, 4); */
    /* ftl_test_write(0, 8); */
    /* ftl_test_write(8, 8); */
    /* tc_write_seq(0, 1, NUM_PSECTORS_4KB); */
    /* tc_write_seq(0, 1, NUM_PSECTORS_4KB); */
    tc_write_seq(0, 100000, NUM_PSECTORS_128KB);
    tc_write_seq(0, 100000, NUM_PSECTORS_128KB);

    while (end == 0);
}
static void tc_write_seq(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size)
{
    UINT32 io_cnt;
    UINT32 lsn;

    lsn = start_lsn;

    for (io_cnt = 0; io_cnt < io_num; io_cnt++)
    {
        ftl_test_write(lsn, sector_size);

        lsn += sector_size;

        if (lsn >= io_limit)
        {
            lsn %= io_limit;
        }
    }
}
static void tc_write_rand(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size)
{
    UINT32 io_cnt;
    UINT32 lsn;

    lsn = start_lsn;

    // TODO
    /* srand(time(NULL)); */

    for (io_cnt = 0; io_cnt < io_num; io_cnt++)
    {
        ftl_test_write(lsn, sector_size);

        /* lsn = rand(0, io_limit - 1); */

        if (lsn >= io_limit)
        {
            lsn -= io_limit;
        }
    }
}
#endif
