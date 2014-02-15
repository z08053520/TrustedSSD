#ifndef __FDE_H
#define __FDE_H
#include "jasmine.h"

typedef UINT32 fde_key_t;

void fde_init();
void fde_encrypt(UINT32 const buf, UINT8 const num_sectors, fde_key_t key);
void fde_decrypt(UINT32 const buf, UINT8 const num_sectors, fde_key_t key);

#endif
