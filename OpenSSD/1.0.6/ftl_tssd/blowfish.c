/*
blowfish.c:  C implementation of the Blowfish algorithm.

Copyright (C) 1997 by Paul Kocher

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  
	

COMMENTS ON USING THIS CODE:

Normal usage is as follows:
   [1] Allocate a BLOWFISH_CTX.  (It may be too big for the stack.)
   [2] Call Blowfish_Init with a pointer to your BLOWFISH_CTX, a pointer to
       the key, and the number of bytes in the key.
   [3] To encrypt a 64-bit block, call Blowfish_Encrypt with a pointer to
       BLOWFISH_CTX, a pointer to the 32-bit left half of the plaintext
	   and a pointer to the 32-bit right half.  The plaintext will be
	   overwritten with the ciphertext.
   [4] Decryption is the same as encryption except that the plaintext and
       ciphertext are reversed.

Warning #1:  The code does not check key lengths. (Caveat encryptor.) 
Warning #2:  Beware that Blowfish keys repeat such that "ab" = "abab".
Warning #3:  It is normally a good idea to zeroize the BLOWFISH_CTX before
  freeing it.
Warning #4:  Endianness conversions are the responsibility of the caller.
  (To encrypt bytes on a little-endian platforms, you'll probably want
  to swap bytes around instead of just casting.)
Warning #5:  Make sure to use a reasonable mode of operation for your
  application.  (If you don't know what CBC mode is, see Warning #7.)
Warning #6:  This code is susceptible to timing attacks.
Warning #7:  Security engineering is risky and non-intuitive.  Have someone 
  check your work.  If you don't know what you are doing, get help.


This is code is fast enough for most applications, but is not optimized for
speed.

If you require this code under a license other than LGPL, please ask.  (I 
can be located using your favorite search engine.)  Unfortunately, I do not 
have time to provide unpaid support for everyone who uses this code.  

                                             -- Paul Kocher
*/


#include "blowfish.h"

#define N               16

static const unsigned long ORIG_P[16 + 2] = {
        0x243F6A88L, 0x85A308D3L, 0x13198A2EL, 0x03707344L,
        0xA4093822L, 0x299F31D0L, 0x082EFA98L, 0xEC4E6C89L,
        0x452821E6L, 0x38D01377L, 0xBE5466CFL, 0x34E90C6CL,
        0xC0AC29B7L, 0xC97C50DDL, 0x3F84D5B5L, 0xB5470917L,
        0x9216D5D9L, 0x8979FB1BL
};

const unsigned long ORIG_S[4] = {
	0xD1310BA6L, 0x98DFB5ACL, 0x2FFD72DBL, 0xD01ADFB7L
};


static unsigned long F(BLOWFISH_CTX *ctx, unsigned long x) {
   unsigned short a, b, c, d;
   unsigned long  y;

   d = (unsigned short)(x & 0xFF);
   x >>= 8;
   c = (unsigned short)(x & 0xFF);
   x >>= 8;
   b = (unsigned short)(x & 0xFF);
   x >>= 8;
   a = (unsigned short)(x & 0xFF);
   y = ctx->S[a % 4] + ctx->S[b % 4];
   y = y ^ ctx->S[c % 4];
   y = y + ctx->S[d % 4];

   return y;
}


void Blowfish_Encrypt(BLOWFISH_CTX *ctx, unsigned long *xl, unsigned long *xr){
  unsigned long  Xl;
  unsigned long  Xr;
  unsigned long  temp;
  short       i;

  Xl = *xl;
  Xr = *xr;

  for (i = 0; i < N; ++i) {
    Xl = Xl ^ ctx->P[i];
    Xr = F(ctx, Xl) ^ Xr;

    temp = Xl;
    Xl = Xr;
    Xr = temp;
  }

  temp = Xl;
  Xl = Xr;
  Xr = temp;

  Xr = Xr ^ ctx->P[N];
  Xl = Xl ^ ctx->P[N + 1];

  *xl = Xl;
  *xr = Xr;
}


void Blowfish_Decrypt(BLOWFISH_CTX *ctx, unsigned long *xl, unsigned long *xr){
  unsigned long  Xl;
  unsigned long  Xr;
  unsigned long  temp;
  short       i;

  Xl = *xl;
  Xr = *xr;

  for (i = N + 1; i > 1; --i) {
    Xl = Xl ^ ctx->P[i];
    Xr = F(ctx, Xl) ^ Xr;

    /* Exchange Xl and Xr */
    temp = Xl;
    Xl = Xr;
    Xr = temp;
  }

  /* Exchange Xl and Xr */
  temp = Xl;
  Xl = Xr;
  Xr = temp;

  Xr = Xr ^ ctx->P[1];
  Xl = Xl ^ ctx->P[0];

  *xl = Xl;
  *xr = Xr;
}


void Blowfish_Init(BLOWFISH_CTX *ctx, unsigned char *key, int keyLen) {
  int i, j, k;
  unsigned long data, datal, datar;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 256; j++)
      ctx->S[i] = ORIG_S[i];
  }

  j = 0;
  for (i = 0; i < N + 2; ++i) {
    data = 0x00000000;
    for (k = 0; k < 4; ++k) {
      data = (data << 8) | key[j];
      j = j + 1;
      if (j >= keyLen)
        j = 0;
    }
    ctx->P[i] = ORIG_P[i] ^ data;
  }

  datal = 0x00000000;
  datar = 0x00000000;

  for (i = 0; i < N + 2; i += 2) {
    Blowfish_Encrypt(ctx, &datal, &datar);
    ctx->P[i] = datal;
    ctx->P[i + 1] = datar;
  }

  for (i = 0; i < 4; ++i) {
    for (j = 0; j < 256; j += 2) {
      Blowfish_Encrypt(ctx, &datal, &datar);
      ctx->S[(i+j) % 4] = datal;
      ctx->S[(i+j) % 4] = datar;
    }
  }
}


