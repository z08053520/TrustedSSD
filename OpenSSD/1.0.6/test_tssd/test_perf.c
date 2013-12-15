/* ===========================================================================
 * Test the performance of FTL, raw flash operations, DRAM and SRAM 
 * =========================================================================*/

#include "jasmine.h"
#include "flash.h"
#include "bad_blocks.h"
#include "gc.h"
#include "test_util.h"

#if OPTION_FTL_TEST

static void sram_perf_test()
{
	uart_print("SRAM performance test begins...");

	UINT32 sram_begin = SRAM_BASE,
	       sram_end   = SRAM_BASE + SRAM_SIZE;
	UINT32 sram_addr  = sram_begin;
	
	UINT32 total_operations = 1024 * 1024 * 1024;
	UINT32 num_operations   = 0;

	UINT32 sum = 0;
	timer_reset();
	while (num_operations < total_operations) {
		sum += GETREG(sram_addr);

		sram_addr += sizeof(UINT32);
		if (sram_addr >= sram_end) sram_addr = sram_begin;

		num_operations ++;
	}
	UINT32 time_us = timer_ellapsed_us();
	uart_printf("SRAM throughput = %uMB/s, latency = %dus/s\r\n",
		    total_operations / 1000 / time_us * sizeof(UINT32),
		    1.0 * time_us / total_operations);

	uart_print("Done");
}

static void dram_perf_test()
{
	uart_print("DRAM performance test begins...");

	UINT32 bank;
	UINT32 time_us;

	uart_print("First, test throughput");

	UINT32 num_bytes_to_copy = 1024 * 1024 * 1024; // 1GB
	UINT32 num_pages_to_copy = num_bytes_to_copy / BYTES_PER_PAGE;
	UINT32 num_pages_copied  = 0;
	bank = 0;
	timer_reset();
	while (num_pages_copied < num_pages_to_copy) {
		mem_copy(FTL_BUF(bank), TEMP_BUF_ADDR, BYTES_PER_PAGE);

		bank = (bank + 1) % NUM_BANKS;
		num_pages_copied++;
	}
	time_us = timer_ellapsed_us();
	uart_printf("DRAM copy throughput = %uMB/s (time = %ums)\r\n", 
		    num_bytes_to_copy / time_us, time_us / 1000);

	uart_print("Next, test latency");
	UINT32 total_mem_operations = 1024 * 1024;
	UINT32 addr_begin = BUFS_ADDR, addr_end = HIL_BUF_ADDR;
	UINT32 addr = addr_begin;
	UINT32 num_mem_operations  = 0;
	timer_reset();
	while (num_mem_operations < total_mem_operations) {
		write_dram_32(addr, addr);
		read_dram_32(addr);
		
		addr += sizeof(UINT32);
		if (addr >= end_addr) addr = begin_addr;	

		num_mem_operations += 2;	// read + write
	}
	time_us = timer_ellapsed_us();
	uart_printf("DRAM access latency = %dus\r\n", 
		    1.0 * time_us / total_mem_operations);

	uart_print("Done");
}

static void flash_perf_test()
{
	uart_print("Raw flash operation performance test begins...");

	UINT32 bank, vpn;
	UINT32 vpns[NUM_BANKS];

	uart_print("First, test throughput");

	UINT32 total_mb_thr		= 256;
	UINT32 total_sectors_thr	= total_mb_thr * 1024 * 1024 / BYTES_PER_SECTOR;
	UINT32 num_sectors_so_far;
	
	uart_printf("Write %uMB data parallelly into all banks\r\n", total_mb_thr);
	num_sectors_so_far = 0;
	perf_monitor_reset();
	while (num_sectors_so_far > total_sectors_thr) {
		FOR_EACH_BANK(bank) {
			vpn = gc_allocate_new_vpn(bank);	

			nand_page_program_from_host(bank, 
						    vpn / PAGES_PER_VBLK, 
						    vpn % PAGES_PER_VBLK)

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
		}
	}
	perf_monitor_update(num_sectors_so_far);

	uart_print("Next, test latency");

	UINT32 total_pages = 64 * 1024, num_pages_so_far;
	UINT32 time_us;
	UINT32 i;
	UINT32 sectors[] = {1, SECTORS_PER_PAGE}; // 1 sector, 1 page

	for(i = 0; i < 2; i++) {
		uart_printf("Synchronously write individual %s one by one to a bank\r\n",
			   i == 0 ? "sector" : "page");
		timer_reset();	
		bank = 0;
		num_pages_so_far = 0;
		while (num_pages_so_far < total_pages) {
			vpn = gc_allocate_new_vpn(bank);	
		
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
		}
		time_us = timer_ellapsed_us();
		uart_printf("Flash read latency = %uus (total time = %ums)\r\n", 
			    time_us / total_pages, time_us / 1000);
	}

	uart_print("Done");
}

static void ftl_perf_test(UINT32 const num_sectors)
{
	uart_printf("FTL performance test (unit = %u bytes) begins...\r\n", 
		    num_sectors * BYTES_PER_SECTOR);

	UINT32 total_mb		= 256;
	UINT32 total_sectors 	= total_mb * 1024 * 1024 / BYTES_PER_SECTOR;

	UINT32 lba, end_lba = total_sectors;
	UINT32 i;
	for(i = 0; i < 2; i++) {
		uart_printf("Round %u\r\n", i);

		// read
		uart_printf("Read sequentially %uMB of data", total_mb);
		lba = 0;
		perf_monitor_reset();
		while (lba < end_lba) {
			ftl_read(lba, num_sectors);

			lba += num_sectors;
		}
		perf_monitor_udpate(total_sectors);
		// write
		uart_printf("Write sequentially %uMB of data", total_mb);
		lba = 0;
		perf_monitor_reset();
		while (lba < end_lba) {
			ftl_write(lba, num_sectors);
		
			lba += num_sectors;
		}
		perf_monitor_udpate(total_sectors);
	}

	uart_print("Done");
}

void ftl_test()
{
	ftl_perf_test();	
	flash_perf_test();
	dram_perf_test();
	sram_perf_test();
}

#endif
