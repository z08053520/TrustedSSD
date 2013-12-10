/* ===========================================================================
 * Unit test for Page Mapping Table (PMT) 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "pmt.h"
#include "gc.h"
#include "mem_util.h"
#include <stdlib.h>

#define RAND_SEED	1234
#define MAX_LPN		NUM_LPAGES

#define BUF_SIZE	(BYTES_PER_SECTOR * SECTORS_PER_PAGE / 2)
#define LPN_BUF		TEMP_BUF_ADDR
#define VPN_BUF		(TEMP_BUF_ADDR + BUF_SIZE)

// a sample size that much bigger than the capacity of CMT or BC
#define MAX_SAMPLE_SIZE	(BUF_SIZE / sizeof(UINT32))	/* 2K */
#define SAMPLE_SIZE	MAX_SAMPLE_SIZE

static void init_dram()
{
	mem_set_dram(LPN_BUF, 0, BUF_SIZE);
	mem_set_dram(VPN_BUF, 0, BUF_SIZE);
}

static UINT32 get_lpn(UINT32 const i)
{
	return read_dram_32(LPN_BUF + sizeof(UINT32) * i);
}

static void set_lpn(UINT32 const i, UINT32 const lpn)
{
	write_dram_32(LPN_BUF + sizeof(UINT32) * i, lpn);
}

static UINT32 get_vpn(UINT32 const i)
{
	return read_dram_32(VPN_BUF + sizeof(UINT32) * i);
}	

static void set_vpn(UINT32 const i, UINT32 const vpn)
{
	write_dram_32(VPN_BUF + sizeof(UINT32) * i, vpn);
}

void ftl_test()
{
	UINT32 i;
	UINT32 j, repeats = 128, sample_size;
	UINT32 lpn, vpn, expected_vpn;

	uart_print("Running unit test for PMT... ");
	uart_printf("sample size = %d\r\n", SAMPLE_SIZE);

	srand(RAND_SEED);
	init_dram();
  	
	uart_print("\tsample lpn to check vpn is set to 0 by default");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lpn = rand() % MAX_LPN;
		pmt_fetch(lpn, &vpn);
		BUG_ON("vpn of an unused lpn is not 0", vpn != 0);
	}

	lpn = 0;
	for(j = 0; j < repeats; j++) {
		sample_size = rand() % SAMPLE_SIZE+ 1;

		uart_print("\tupdate lpns");
		for(i = 0; i < sample_size; i++) {
			//vpn = gc_allocate_new_vpn(lpn2bank(lpn));
			vpn = 32 + rand() % 123456;

			set_lpn(i, lpn);
			set_vpn(i, vpn);

			pmt_update(lpn, vpn);
			lpn++;
			//uart_printf("%d: set lpn = %d, vpn = %d\r\n", i, lpn, vpn);
		}
		
		uart_print("\tverify lpn->vpn");
		for(i = 0; i < sample_size; i++) {
			lpn = get_lpn(i);
			expected_vpn = get_vpn(i);

			pmt_fetch(lpn, &vpn);
			//uart_printf("%d: get lpn = %d, vpn = %d, expected_vpn = %d\r\n", 
					//i, lpn, vpn, expected_vpn);
			BUG_ON("vpn is not as expected", vpn != expected_vpn);
		}
	}	
	uart_print("PMT passed the unit test ^_^");
}

#endif
