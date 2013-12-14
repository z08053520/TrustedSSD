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

}

static void dram_perf_test()
{

}

static void flash_perf_test()
{
	uart_print("Raw flash operation performance test begin...");

	UINT32 bank, vpn;

	uart_print("First, do throughput test");
	UINT32 total_mb_thr		= 256;
	UINT32 total_sectors_thr	= total_mb_thr * 1024 * 1024 / BYTES_PER_SECTOR;
	UINT32 num_sectors_so_far;
	
	uart_printf("Write %uMB data parallelly into all banks\r\n", total_mb_thr);
	num_sectors_so_far = 0;
	perf_monitor_reset();
	FOR_EACH_BANK(bank) {
		vpn = gc_allocate_new_vpn(bank);	

		nand_page_program_from_host(bank, 
				  	    vpn / PAGES_PER_VBLK, 
				  	    vpn % PAGES_PER_VBLK)

		num_sectors_so_far += SECTORS_PER_PAGE;
		if (num_sectors_so_far > total_sectors_thr) break;
	}
	perf_monitor_update(num_sectors_so_far);

	uart_printf("Read %uMB data parallelly from all banks\r\n", total_mb_thr);
	num_sectors_so_far = 0;
	perf_monitor_reset();
	FOR_EACH_BANK(bank) {
		while (vpns[bank] % PAGES_PER_VBLK == 0 && 
		       bb_is_bad(bank, vpns[bank] / PAGES_PER_VBLK)) {
			vpns[bank] += PAGES_PER_VBLK;
		}

		nand_page_read_to_host(bank, 
				       vpn / PAGES_PER_VBLK, 
				       vpn % PAGES_PER_VBLK); 

		num_sectors_so_far += SECTORS_PER_PAGE;
		if (num_sectors_so_far > total_sectors_thr) break;

		vpns[bank]++;
	}
	perf_monitor_update(num_sectors_so_far);

	uart_print("Next, do latency test");
	UINT32 vpns[NUM_BANKS];
	mem_set_sram(vpns, 0, NUM_BANKS * sizeof(UINT32));
	UINT32 total_pages = 16 * 1024, num_pages_so_far;

	uart_print("Synchronously write individual page one by one to a bank");
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
					      1);
		// wait until the new command is accepted by the target bank
		while ((GETREG(WR_STAT) & 0x00000001) != 0);
		// wail until the target bank finishes the command
		while (BSP_FSM(bank) != BANK_IDLE);

		num_pages_so_far ++;
	}
	UINT32 time_us = timer_ellapsed_us();
	uart_printf("Flash write latency = %u", time_us / total_pages);

	uart_print("Synchronously read individual page one by one from a bank");
	UINT32 total_pages = 16 * 1024, num_pages_so_far;
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
					 1);
		// wait until the new command is accepted by the target bank
		while ((GETREG(WR_STAT) & 0x00000001) != 0);
		// wail until the target bank finishes the command
		while (BSP_FSM(bank) != BANK_IDLE);

		num_pages_so_far ++;
	}
	UINT32 time_us = timer_ellapsed_us();
	uart_printf("Flash write latency = %u", time_us / total_pages);

	uart_print("Done");
}

static void ftl_perf_test(UINT32 const num_sectors)
{
	uart_printf("FTL performance test (%ubytes) begin...\r\n", 
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
