/* ===========================================================================
 * Test the performance of FTL, raw flash operations, DRAM and SRAM
 * =========================================================================*/

#if OPTION_FTL_TEST
#include "jasmine.h"
#include "dram.h"
#include "ftl.h"
#include "bad_blocks.h"
#include "gc.h"
#include "test_util.h"

extern BOOL8 	eventq_put(UINT32 const lba, UINT32 const num_sectors,
#if OPTION_ACL
				UINT32 const session_key,
#endif
				UINT32 const cmd_type);
extern BOOL8 ftl_all_sata_cmd_accepted();

static void sram_perf_test()
{
	uart_print("SRAM performance test begins...");

	UINT32 sram_begin = SRAM_BASE,
	       sram_end   = SRAM_BASE + 4096;
	UINT32 sram_addr  = sram_begin;

	UINT32 total_operations = 64 * 1024;
	UINT32 num_operations   = 0;
	UINT32 bank, rbank;

	// testing SRAM access
	UINT32 sum = 0;
	timer_reset();
	while (num_operations < total_operations) {
		sum += GETREG(sram_addr);

		sram_addr += sizeof(UINT32);
		if (sram_addr >= sram_end) sram_addr = sram_begin;

		num_operations ++;
	}
	UINT32 time_us = timer_ellapsed_us();
	uart_printf("SRAM throughput = %uMB/s, latency = %uns\r\n",
		    total_operations * sizeof(UINT32) / time_us,
		    1000 * time_us / total_operations);

	// testing flash register
	flash_finish();
	num_operations = 0;
	timer_reset();
	while (num_operations < total_operations) {
		bank = 0;
		while (rbank = REAL_BANK(bank),
		       bank < NUM_BANKS && _BSP_FSM(rbank) == BANK_IDLE)
			bank++;

		num_operations++;
	}
	time_us = timer_ellapsed_us();
	uart_printf("Iterating flash state registers latency = %uns\r\n",
		    1000 * time_us / total_operations);
}

static void dram_perf_test()
{
	uart_print("DRAM performance test begins...");

	UINT32 bank;
	UINT32 time_us;

	uart_print("First, test throughput");

	UINT32 num_bytes_to_copy = 2 << 30; // 2GB
	UINT32 num_pages_to_copy = num_bytes_to_copy / BYTES_PER_PAGE;
	UINT32 num_pages_copied  = 0;
	bank = 0;
	timer_reset();
	while (num_pages_copied < num_pages_to_copy) {
		mem_copy(COPY_BUF(bank), TEMP_BUF_ADDR, BYTES_PER_PAGE);

		bank = (bank + 1) % NUM_BANKS;
		num_pages_copied++;
	}
	time_us = timer_ellapsed_us();
	uart_printf("DRAM copy throughput = %uMB/s (latency = %uus)\r\n",
		    num_bytes_to_copy / time_us, time_us / num_pages_copied);

	UINT32 total_mem_operations = 1024 * 1024;
	uart_print("Test read latency");
	timer_reset();
	for (UINT32 num_mem_operations = 0,
		begin_addr = HIL_BUF_ADDR,
		end_addr = (UINT32) (HIL_BUF_ADDR + BYTES_PER_BANK),
		addr = begin_addr;
		num_mem_operations < total_mem_operations;
		num_mem_operations++, addr += sizeof(UINT32)) {
		if (addr >= end_addr) addr = begin_addr;

		read_dram_32(addr);
	}
	time_us = timer_ellapsed_us();
	uart_print("DRAM read latency = %uns",
		    1000 * time_us / total_mem_operations);

	uart_print("Test write latency");
	timer_reset();
	for (UINT32 num_mem_operations = 0,
		begin_addr = HIL_BUF_ADDR,
		end_addr = (UINT32)(HIL_BUF_ADDR + BYTES_PER_BANK),
		addr = begin_addr;
		num_mem_operations < total_mem_operations;
		num_mem_operations++, addr += sizeof(UINT32)) {
		if (addr >= end_addr) addr = begin_addr;

		write_dram_32(addr, addr);
	}
	time_us = timer_ellapsed_us();
	uart_print("DRAM write latency = %uns",
		    1000 * time_us / total_mem_operations);

	uart_print("Done");
}

static void flash_perf_test(UINT32 const total_mb_thr)
{
	uart_print("Raw flash operation performance test begins...");

	UINT32 bank, vpn;
	UINT32 vpns[NUM_BANKS];

	uart_print("First, test throughput");

	UINT32 total_sectors_thr	= total_mb_thr * 1024 * 1024 / BYTES_PER_SECTOR;
	UINT32 num_sectors_so_far;

	uart_printf("Write %uMB data parallelly into all banks\r\n", total_mb_thr);
	num_sectors_so_far = 0;
	perf_monitor_reset();
	while (num_sectors_so_far < total_sectors_thr) {
		FOR_EACH_BANK(bank) {
			vpn = gc_allocate_new_vpn(bank, FALSE);

			nand_page_program_from_host(bank,
						    vpn / PAGES_PER_VBLK,
						    vpn % PAGES_PER_VBLK);

			num_sectors_so_far += SECTORS_PER_PAGE;
		}
		// flash_finish();
	}
	perf_monitor_update(num_sectors_so_far);

	uart_printf("Read %uMB data parallelly from all banks\r\n", total_mb_thr);
	mem_set_sram(vpns, 0, NUM_BANKS * sizeof(UINT32));
	num_sectors_so_far = 0;
	perf_monitor_reset();
	while (num_sectors_so_far < total_sectors_thr) {
		FOR_EACH_BANK(bank) {
			while (vpns[bank] % PAGES_PER_VBLK == 0 &&
			       bb_is_bad(bank, vpns[bank] / PAGES_PER_VBLK)) {
				vpns[bank] += PAGES_PER_VBLK;
			}
			vpn = vpns[bank];

			nand_page_read_to_host(bank,
					       vpn / PAGES_PER_VBLK,
					       vpn % PAGES_PER_VBLK);

			vpns[bank]++;
			num_sectors_so_far += SECTORS_PER_PAGE;
		}
	}
	perf_monitor_update(num_sectors_so_far);

	uart_print("Next, test latency");

	UINT32 total_pages = 12 * 1024, num_pages_so_far;
	UINT32 time_us;
	UINT32 i;
	UINT32 sectors[] = {1, SECTORS_PER_PAGE}; // 1 sector, 1 page

	// Measure latency for one sector and one page, respectively
	for(i = 0; i < 2; i++) {
		uart_printf("Synchronously write individual %s one by one to a bank\r\n",
			   i == 0 ? "sector" : "page");
		timer_reset();
		bank = 0;
		num_pages_so_far = 0;
		while (num_pages_so_far < total_pages) {
			vpn = gc_allocate_new_vpn(bank, FALSE);

			// only write one sector
			nand_page_ptprogram_from_host(bank,
						      vpn / PAGES_PER_VBLK,
						      vpn % PAGES_PER_VBLK,
						      0,
						      sectors[i]);
			// wait until the new command is accepted by the target bank
			while ((GETREG(WR_STAT) & 0x00000001) != 0);
			// wail until the target bank finishes the command
			while (BSP_FSM(bank) != BANK_IDLE);

			num_pages_so_far ++;
		}
		time_us = timer_ellapsed_us();
		uart_printf("Flash write latency = %uus (total time = %ums)\r\n",
			    time_us / total_pages, time_us / 1000);

		uart_printf("Synchronously read individual %s one by one from a bank\r\n",
			   i == 0 ? "sector" : "page");
		timer_reset();
		bank = 0;
		vpn = 0;
		num_pages_so_far = 0;
		while (num_pages_so_far < total_pages) {
			while (vpn % PAGES_PER_VBLK == 0 &&
			       bb_is_bad(bank, vpn / PAGES_PER_VBLK)) {
				vpn += PAGES_PER_VBLK;
			}

			// only read one sector
			nand_page_ptread_to_host(bank,
						 vpn / PAGES_PER_VBLK,
						 vpn % PAGES_PER_VBLK,
						 0,
						 sectors[i]);
			// wait until the new command is accepted by the target bank
			while ((GETREG(WR_STAT) & 0x00000001) != 0);
			// wail until the target bank finishes the command
			while (BSP_FSM(bank) != BANK_IDLE);

			num_pages_so_far ++;
			vpn ++;
		}
		time_us = timer_ellapsed_us();
		uart_printf("Flash read latency = %uus (total time = %ums)\r\n",
			    time_us / total_pages, time_us / 1000);
	}

	uart_print("Done");
}

//#define FTL_REQ_UNALIGNED
#ifdef  FTL_REQ_UNALIGNED
	#define LBA_BEGIN	3
#else
	#define LBA_BEGIN	0
#endif

static UINT32 g_seq_total_mb = 0;

static UINT32 const ONE_SESSION_KEY = 1234;

static void ftl_finish_all()
{
	BOOL8 idle;
	do {
		idle = ftl_main();
	} while(!idle);
}

static void ftl_read(UINT32 const lba, UINT32 const req_sectors)
{
#if OPTION_ACL
	while(eventq_put(lba, req_sectors, ONE_SESSION_KEY, READ))
#else
	while(eventq_put(lba, req_sectors, READ))
#endif
		ftl_main();
	/* Make sure it is accepted and proccessed */
	while (!ftl_all_sata_cmd_accepted())
		ftl_main();
}

static void ftl_write(UINT32 const lba, UINT32 const req_sectors)
{
#if OPTION_ACL
	while(eventq_put(lba, req_sectors, ONE_SESSION_KEY, WRITE))
#else
	while(eventq_put(lba, req_sectors, WRITE))
#endif
		ftl_main();
	/* Make sure it is accepted and proccessed */
	while (!ftl_all_sata_cmd_accepted())
		ftl_main();
}

static void ftl_perf_test_seq(UINT32 const num_sectors, UINT32 const total_mb)
{
	uart_printf("FTL **sequential** read/write test (unit = %u bytes) begins...\r\n",
		    num_sectors * BYTES_PER_SECTOR);

	UINT32 total_sectors 	= total_mb * 1024 * 1024 / BYTES_PER_SECTOR;

	UINT32 lba,
	       begin_lba = LBA_BEGIN + g_seq_total_mb * 1024 / 1024 / BYTES_PER_SECTOR,
	       end_lba = total_sectors;

	begin_lba = begin_lba / num_sectors * num_sectors;

	g_seq_total_mb += total_mb;

	// write
	uart_printf("Write sequentially %uMB of data\r\n", total_mb);
	lba = begin_lba;
	perf_monitor_reset();
	while (lba < end_lba) {
		ftl_write(lba, num_sectors);

		lba += num_sectors;
	}
	ftl_finish_all();
	perf_monitor_update(total_sectors);

	// read
	uart_printf("Read sequentially %uMB of data\r\n", total_mb);
	lba = begin_lba;
	perf_monitor_reset();
	while (lba < end_lba) {
		ftl_read(lba, num_sectors);

		lba += num_sectors;
	}
	ftl_finish_all();
	perf_monitor_update(total_sectors);

	uart_print("Done");
}

static void ftl_perf_test_rnd(UINT32 const num_sectors, UINT32 const total_mb)
{
	uart_printf("FTL **random** read/write test (unit = %u bytes) begins...\r\n",
		    num_sectors * BYTES_PER_SECTOR);

	UINT32 lba, end_lba = g_seq_total_mb * 1024 * 1024 / BYTES_PER_SECTOR;
	UINT32 num_sectors_so_far, total_num_sectors = total_mb * 1024 * 1024 / BYTES_PER_SECTOR;

	// write
	uart_printf("Write randomly %uMB of data\r\n", total_mb);
	perf_monitor_reset();
	num_sectors_so_far = 0;
	while (num_sectors_so_far < total_num_sectors) {
		lba = random(0, end_lba);
		lba = lba / num_sectors * num_sectors; // align with req size

		ftl_write(lba, num_sectors);

		num_sectors_so_far += num_sectors;
	}
	ftl_finish_all();
	perf_monitor_update(total_num_sectors);

	// read
	uart_printf("Read randomly %uMB of data\r\n", total_mb);
	perf_monitor_reset();
	num_sectors_so_far = 0;
	while (num_sectors_so_far < total_num_sectors) {
		lba = random(0, end_lba);
		lba = lba / num_sectors * num_sectors; // align with req size

		ftl_read(lba, num_sectors);

		num_sectors_so_far += num_sectors;;
	}
	ftl_finish_all();
	perf_monitor_update(total_num_sectors);

	uart_print("Done");
}

void ftl_test()
{
	uart_print("Performance test begins...");
	uart_print("------------------------ SRAM ---------------------------");
//		sram_perf_test();
	uart_print("------------------------ DRAM ---------------------------");
		/* dram_perf_test(); */
	uart_print("---------------------- Raw Flash ------------------------");
		//UINT32 total_mb_thr = 512;
		//flash_perf_test(total_mb_thr);
	uart_print("--------------------- FTL Seq R/W -----------------------");
		UINT32 total_mb_seq = 256;
		ftl_perf_test_seq(8,  total_mb_seq);	// req stride -- 4KB
		ftl_perf_test_seq(32, total_mb_seq);	// req stride -- 16KB
		ftl_perf_test_seq(64, total_mb_seq);	// req stride -- 32KB
	uart_print("--------------------- FTL Rnd R/W -----------------------");
		UINT32 total_mb_rnd = 128;
		ftl_perf_test_rnd(8,  total_mb_rnd);	// req stride -- 4KB
		ftl_perf_test_rnd(32, total_mb_rnd);	// req stride -- 16KB
		ftl_perf_test_rnd(64, total_mb_rnd);	// req stride -- 32KB
	uart_print("--------------------------------------------------------");
	uart_print("Performance test is done ^_^");
}

#endif
