/* ===========================================================================
 * Unit test for FDE (Full Disk Encryption)
 * =========================================================================*/
#include "jasmine.h"

#if OPTION_FTL_TEST
#if OPTION_FDE
#include "fde.h"
#include "test_util.h"
#include "mem_util.h"
#include "dram.h"
#include "stdlib.h"

#define RAND_SEED	123456
#define NUM_TRIALS	32

#define ENCRYPTED_BUF	COPY_BUF(0)
#define PLAINTEXT_BUF 	COPY_BUF(1)

static void fill_buffer_randomly(UINT32 const buf, UINT8 const num_sectors)
{
	UINT32 addr = buf, addr_end = buf + num_sectors * BYTES_PER_SECTOR;
	while (addr < addr_end) {
		UINT32 val = rand();
		write_dram_32(addr, val);

		addr += sizeof(UINT32);
	}
}

static BOOL8 is_buffer_same(UINT32 const buf_a, UINT32 const buf_b, 
			    UINT8 const num_sectors)
{
	return mem_cmp_dram(buf_a, buf_b, num_sectors * BYTES_PER_SECTOR) == 0;
}

static void copy_buffer(UINT32 const target_buf, 
			UINT32 const src_buf,
			UINT8 const num_sectors)
{
	mem_copy(target_buf, src_buf, num_sectors * BYTES_PER_SECTOR);
}

void ftl_test()
{
	uart_print("Start testing FDE...");

	UINT8 trial_i;
	for (trial_i = 0; trial_i < NUM_TRIALS; trial_i++) {
		UINT8 num_sectors = random(1, SECTORS_PER_PAGE);
		fill_buffer_randomly(PLAINTEXT_BUF, num_sectors);
		
		srand(RAND_SEED);
		fde_key_t key, wrong_key;
		int j;
		for (j = 0; j < 32; ++j) {
			int t = rand();
			t = (t > 0) ? t : (-t);
			key.key[j] = (t + 'c') % 256;
			wrong_key.key[j] = (t + 'j') % 256;
		}

		/* Using wrong key to decrypt should NOT work */
		copy_buffer(ENCRYPTED_BUF, PLAINTEXT_BUF, num_sectors);
		
		fde_encrypt(ENCRYPTED_BUF, num_sectors, key);
		fde_decrypt(ENCRYPTED_BUF, num_sectors, wrong_key);

		BUG_ON("Decrypted text should NOT be the same as "
		       "the origin plain text", 
		       is_buffer_same(ENCRYPTED_BUF, PLAINTEXT_BUF, num_sectors));

		/* Using correct key to decrypt should work */
		copy_buffer(ENCRYPTED_BUF, PLAINTEXT_BUF, num_sectors);

		fde_encrypt(ENCRYPTED_BUF, num_sectors, key);
		fde_decrypt(ENCRYPTED_BUF, num_sectors, key);

		BUG_ON("Decrypted text should be the same as "
		       "the origin plain text", 
		       !is_buffer_same(ENCRYPTED_BUF, PLAINTEXT_BUF, num_sectors));
	}

	uart_print("FDE passed the unit test ^_^");
}

#endif
#endif
