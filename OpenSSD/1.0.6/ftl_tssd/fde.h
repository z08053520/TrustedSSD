#ifndef __FDE_H
#define __FDE_H
#include "jasmine.h"
#include "aes256.h"
#include "blowfish.h"

#define AES			0
#define BLOWFISH		1
/* #define AES_BLOWFISH_SWITCHER 	BLOWFISH */	
#define AES_BLOWFISH_SWITCHER 	AES	


typedef union _u8_to_ul_t{
	unsigned long xl;
	unsigned long xr;
	uint8_t  ivec[4];
} u8_to_ul_t;

typedef struct _key_t {
	unsigned long aux_key_ul[4];
	uint8_t	      aux_key_ui[16]; 
} key_t;

typedef union _fde_key_t {
	uint8_t key[32];
	key_t   aux_key;
} fde_key_t;

void fde_init();
void fde_encrypt(UINT32 const buf, UINT8 const num_sectors, fde_key_t key);
void fde_decrypt(UINT32 const buf, UINT8 const num_sectors, fde_key_t key);

#endif
