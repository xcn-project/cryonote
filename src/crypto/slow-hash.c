// Copyright (c) 2018, The Bitnote Developers.
// Portions Copyright (c) 2012-2013, The CryptoNote Developers.
//
// All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "aesb.h"
#include "common/int-util.h"
#include "hash-ops.h"
#include "oaes_lib.h"

#include <emmintrin.h>

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#include <intrin.h>
#define STATIC
#define INLINE __inline
#if !defined(RDATA_ALIGN16)
#define RDATA_ALIGN16 __declspec(align(16))
#endif
#elif defined(__MINGW32__)
#include <intrin.h>
#include <windows.h>
#define STATIC static
#define INLINE inline
#if !defined(RDATA_ALIGN16)
#define RDATA_ALIGN16 __attribute__ ((aligned(16)))
#endif
#else
#include <wmmintrin.h>
#define STATIC static
#define INLINE inline
#if !defined(RDATA_ALIGN16)
#define RDATA_ALIGN16 __attribute__ ((aligned(16)))
#endif
#endif

#define MEMORY         (1 << 21) // 2MB scratchpad
#define MEMORY_DARK    (1 << 19) // 0.5MB scratchpad
#define ITER           (1 << 20)
#define ITER_DARK      (1 << 18)
#define AES_BLOCK_SIZE  16
#define AES_KEY_SIZE    32
#define INIT_SIZE_BLK   8
#define INIT_SIZE_BYTE (INIT_SIZE_BLK * AES_BLOCK_SIZE)
#define TOTALBLOCKS (MEMORY / AES_BLOCK_SIZE)
#define TOTALBLOCKS_DARK (MEMORY_DARK / AES_BLOCK_SIZE)
#define state_index(x,dark) (((*((uint64_t *)x) >> 4) & ((dark ? TOTALBLOCKS_DARK : TOTALBLOCKS) - 1)) << 4)

#define U64(x) ((uint64_t *) (x))
#define R128(x) ((__m128i *) (x))

#pragma pack(push, 1)
union cn_slow_hash_state
{
  union hash_state hs;
  struct
  {
    uint8_t k[64];
    uint8_t init[INIT_SIZE_BYTE];
  };
};
#pragma pack(pop)

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#define cpuid(info,x) __cpuidex(info,x,0)
#else
void cpuid(int CPUInfo[4], int InfoType)
{
  __asm__ __volatile__
  (
    "cpuid":
    "=a" (CPUInfo[0]),
    "=b" (CPUInfo[1]),
    "=c" (CPUInfo[2]),
    "=d" (CPUInfo[3]) :
    "a" (InfoType), "c" (0)
  );
}
#endif

STATIC INLINE void mul(const uint8_t *a, const uint8_t *b, uint8_t *res)
{
  uint64_t a0, b0;
  uint64_t hi, lo;

  a0 = U64(a)[0];
  b0 = U64(b)[0];
  lo = mul128(a0, b0, &hi);
  U64(res)[0] = hi;
  U64(res)[1] = lo;
}

STATIC INLINE void sum_half_blocks(uint8_t *a, const uint8_t *b)
{
  uint64_t a0, a1, b0, b1;
  a0 = U64(a)[0];
  a1 = U64(a)[1];
  b0 = U64(b)[0];
  b1 = U64(b)[1];
  a0 += b0;
  a1 += b1;
  U64(a)[0] = a0;
  U64(a)[1] = a1;
}

STATIC INLINE void swap_blocks(uint8_t *a, uint8_t *b)
{
  uint64_t t[2];
  U64(t)[0] = U64(a)[0];
  U64(t)[1] = U64(a)[1];
  U64(a)[0] = U64(b)[0];
  U64(a)[1] = U64(b)[1];
  U64(b)[0] = U64(t)[0];
  U64(b)[1] = U64(t)[1];
}

STATIC INLINE void xor_blocks(uint8_t *a, const uint8_t *b)
{
  U64(a)[0] ^= U64(b)[0];
  U64(a)[1] ^= U64(b)[1];
}

STATIC INLINE int check_aes_hw(void)
{
  int cpuid_results[4];
  static int supported = -1;

  if(supported >= 0)
  {
    return supported;
  }

  cpuid(cpuid_results,1);
  return supported = cpuid_results[2] & (1 << 25);
}

STATIC INLINE void aesni_pseudo_round(const uint8_t *in, uint8_t *out,
    const uint8_t *expandedKey)
{
  __m128i *k = R128(expandedKey);
  __m128i d;

  d = _mm_loadu_si128(R128(in));
  d = _mm_aesenc_si128(d, *R128(&k[0]));
  d = _mm_aesenc_si128(d, *R128(&k[1]));
  d = _mm_aesenc_si128(d, *R128(&k[2]));
  d = _mm_aesenc_si128(d, *R128(&k[3]));
  d = _mm_aesenc_si128(d, *R128(&k[4]));
  d = _mm_aesenc_si128(d, *R128(&k[5]));
  d = _mm_aesenc_si128(d, *R128(&k[6]));
  d = _mm_aesenc_si128(d, *R128(&k[7]));
  d = _mm_aesenc_si128(d, *R128(&k[8]));
  d = _mm_aesenc_si128(d, *R128(&k[9]));
  _mm_storeu_si128((R128(out)), d);
}

void cn_slow_hash_internal(const void *data, size_t length, char *hash,
  int dark, uint8_t *long_state)
{
  uint8_t text[INIT_SIZE_BYTE];
  uint8_t a[AES_BLOCK_SIZE];
  uint8_t b[AES_BLOCK_SIZE];
  uint8_t d[AES_BLOCK_SIZE];
  uint8_t aes_key[AES_KEY_SIZE];
  RDATA_ALIGN16 uint8_t expandedKey[256];

  union cn_slow_hash_state state;

  size_t i, j;
  uint8_t *p = NULL;
  oaes_ctx *aes_ctx;

  int useAes = check_aes_hw();
  static void (*const extra_hashes[4])(const void *, size_t, char *) =
  {
    hash_extra_blake, hash_extra_groestl, hash_extra_jh, hash_extra_skein
  };

  hash_process(&state.hs, data, length);
  memcpy(text, state.init, INIT_SIZE_BYTE);

  aes_ctx = (oaes_ctx *) oaes_alloc();
  oaes_key_import_data(aes_ctx, state.hs.b, AES_KEY_SIZE);

  // use aligned data
  memcpy(expandedKey, aes_ctx->key->exp_data, aes_ctx->key->exp_data_len);

  if(useAes)
  {
    for(i = 0; i < (dark ? MEMORY_DARK : MEMORY) / INIT_SIZE_BYTE; i++)
    {
      for(j = 0; j < INIT_SIZE_BLK; j++)
      {
        aesni_pseudo_round(&text[AES_BLOCK_SIZE * j], &text[AES_BLOCK_SIZE * j], expandedKey);
      }

      memcpy(&long_state[i * INIT_SIZE_BYTE], text, INIT_SIZE_BYTE);
    }
  }
  else
  {
    for(i = 0; i < (dark ? MEMORY_DARK : MEMORY) / INIT_SIZE_BYTE; i++)
    {
      for(j = 0; j < INIT_SIZE_BLK; j++)
      {
        aesb_pseudo_round(&text[AES_BLOCK_SIZE * j], &text[AES_BLOCK_SIZE * j], expandedKey);
      }

      memcpy(&long_state[i * INIT_SIZE_BYTE], text, INIT_SIZE_BYTE);
    }
  }

  U64(a)[0] = U64(&state.k[0])[0] ^ U64(&state.k[32])[0];
  U64(a)[1] = U64(&state.k[0])[1] ^ U64(&state.k[32])[1];
  U64(b)[0] = U64(&state.k[16])[0] ^ U64(&state.k[48])[0];
  U64(b)[1] = U64(&state.k[16])[1] ^ U64(&state.k[48])[1];

  for(i = 0; i < (dark ? ITER_DARK : ITER) / 2; i++)
  {
    // Iteration 1
    p = &long_state[state_index(a, dark)];

    if(useAes)
    {
      _mm_storeu_si128(R128(p), _mm_aesenc_si128(_mm_loadu_si128(R128(p)), _mm_loadu_si128(R128(a))));
    }else
    {
      aesb_single_round(p, p, a);
    }

    xor_blocks(b, p);
    swap_blocks(b, p);
    swap_blocks(a, b);

    // Iteration 2
    p = &long_state[state_index(a, dark)];

    mul(a, p, d);
    sum_half_blocks(b, d);
    swap_blocks(b, p);
    xor_blocks(b, p);
    swap_blocks(a, b);
  }

  memcpy(text, state.init, INIT_SIZE_BYTE);
  oaes_key_import_data(aes_ctx, &state.hs.b[32], AES_KEY_SIZE);
  memcpy(expandedKey, aes_ctx->key->exp_data, aes_ctx->key->exp_data_len);
  if(useAes)
  {
    for(i = 0; i < (dark ? MEMORY_DARK : MEMORY) / INIT_SIZE_BYTE; i++)
    {
      for(j = 0; j < INIT_SIZE_BLK; j++)
      {
        xor_blocks(&text[j * AES_BLOCK_SIZE], &long_state[i * INIT_SIZE_BYTE + j * AES_BLOCK_SIZE]);
        aesni_pseudo_round(&text[j * AES_BLOCK_SIZE], &text[j * AES_BLOCK_SIZE], expandedKey);
      }
    }
  }
  else
  {
    for(i = 0; i < (dark ? MEMORY_DARK : MEMORY) / INIT_SIZE_BYTE; i++)
    {
      for(j = 0; j < INIT_SIZE_BLK; j++)
      {
        xor_blocks(&text[j * AES_BLOCK_SIZE], &long_state[i * INIT_SIZE_BYTE + j * AES_BLOCK_SIZE]);
        aesb_pseudo_round(&text[AES_BLOCK_SIZE * j], &text[AES_BLOCK_SIZE * j], expandedKey);
      }
    }
  }

  oaes_free((OAES_CTX **) &aes_ctx);
  memcpy(state.init, text, INIT_SIZE_BYTE);
  hash_permutation(&state.hs);

  if(dark)
  {
    hash_extra_blake(&state, 200, hash);
  }else
  {
    extra_hashes[state.hs.b[0] & 3](&state, 200, hash);
  }
}

void cn_slow_hash_internal_512kb(const void *data, size_t length, char *hash)
{
  uint8_t long_state[MEMORY_DARK];
  cn_slow_hash_internal(data, length, hash, 1, long_state);
}

void cn_slow_hash_internal_2048kb(const void *data, size_t length, char *hash)
{
  uint8_t long_state[MEMORY];
  cn_slow_hash_internal(data, length, hash, 0, long_state);
}

void cn_slow_hash(const void *data, size_t length, char *hash, int dark)
{
  if (dark == 1)
  {
    cn_slow_hash_internal_512kb(data, length, hash);
  }else
  {
    cn_slow_hash_internal_2048kb(data, length, hash);
  }
}
