#include "fde.h"
#include "mem_util.h"

#warning Two ciphers should be implemented in this file 

aes256_context g_aes256_ctx;
BLOWFISH_CTX   g_blowfish_ctx;
fde_key_t      g_key;

void fde_init()
{
	int i;
	for (i = 0; i < 32; ++i) {
		g_key.key[i] = i + 'c';
	}
#if AES == AES_BLOWFISH_SWITCHER
	aes256_init(&g_aes256_ctx, g_key.key);
#elif BLOWFISH == AES_BLOWFISH_SWITCHER
	Blowfish_Init(&g_blowfish_ctx, g_key.key, sizeof(g_key.key));
#endif
}

void fde_encrypt(UINT32 const buf, UINT8 const num_sectors, fde_key_t key)
{
#if AES == AES_BLOWFISH_SWITCHER
	aes256_init(&g_aes256_ctx, key.key);
	uint8_t szbuf[16];
	UINT32 ECB_LEN = 16;
	UINT32 N_ROUND = 512 * num_sectors / ECB_LEN;
	UINT32 idx     = 0;
	while (idx < N_ROUND) {
		mem_copy(szbuf, buf + idx * ECB_LEN, ECB_LEN);
		aes256_encrypt_ecb(&g_aes256_ctx, szbuf);
		mem_copy(buf + idx * ECB_LEN, szbuf, ECB_LEN);
		idx++;
	}
#elif BLOWFISH == AES_BLOWFISH_SWITCHER
	Blowfish_Init(&g_blowfish_ctx, key.key, sizeof(key.key));
	UINT32 len  = 512 * num_sectors;
	UINT32 addr = buf;
	u8_to_ul_t xl, xr;
	while (len) {
		mem_copy(xl.ivec, addr + 0, 4);
		mem_copy(xr.ivec, addr + 4, 4);
		Blowfish_Encrypt(&g_blowfish_ctx, &xl.xl, &xr.xr);
		mem_copy(addr + 0, xl.ivec, 4);
		mem_copy(addr + 4, xr.ivec, 4);
		addr = addr + 8;
		len  = len - 8;
	}
#endif
}

void fde_decrypt(UINT32 const buf, UINT8 const num_sectors, fde_key_t key)
{	
#if AES == AES_BLOWFISH_SWITCHER
	aes256_init(&g_aes256_ctx, key.key);
	uint8_t szbuf[16];
	UINT32 ECB_LEN = 16;
	UINT32 N_ROUND = 512 * num_sectors / ECB_LEN;
	UINT32 idx     = 0;
	while (idx < N_ROUND) {
		mem_copy(szbuf, buf + idx * ECB_LEN, ECB_LEN);
		aes256_decrypt_ecb(&g_aes256_ctx, szbuf);
		mem_copy(buf + idx * ECB_LEN, szbuf, ECB_LEN);
		idx++;
	}
#elif BLOWFISH == AES_BLOWFISH_SWITCHER
	Blowfish_Init(&g_blowfish_ctx, key.key, sizeof(key.key));
	UINT32 len  = 512 * num_sectors;
	UINT32 addr = buf;
	u8_to_ul_t xl, xr;
	while (len) {
		mem_copy(xl.ivec, addr + 0, 4);
		mem_copy(xr.ivec, addr + 4, 4);
		Blowfish_Decrypt(&g_blowfish_ctx, &xl.xl, &xr.xr);
		mem_copy(addr + 0, xl.ivec, 4);
		mem_copy(addr + 4, xr.ivec, 4);
		addr = addr + 8;
		len  = len - 8;
	}
#endif
}
