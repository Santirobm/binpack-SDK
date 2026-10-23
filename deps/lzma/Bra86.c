/* Bra86.c -- Converter for x86 code (BCJ)
2017-04-03 : Igor Pavlov : Public domain */

#include "Precomp.h"
#include "Bra.h"
#include <stdint.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#define Test86MSByte(b) ((((b) + 1) & 0xFE) == 0)

SizeT x86_Convert(Byte *data, SizeT size, UInt32 ip, UInt32 *state, int encoding)
{
  SizeT pos = 0;
  UInt32 mask = *state & 7;
  if (size < 5)
    return 0;
  size -= 4;
  ip += 5;

  for (;;)
  {
    Byte *p = data + pos;
    const Byte *limit = data + size;

#if defined(__AVX2__)
    const __m256i v_e8 = _mm256_set1_epi8((char)0xE8);
    const __m256i v_e9 = _mm256_set1_epi8((char)0xE9);
    while (p + 32 <= limit) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)p);
        __m256i eq = _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_e8), _mm256_cmpeq_epi8(chunk, v_e9));
        uint32_t m = (uint32_t)_mm256_movemask_epi8(eq);
        if (m == 0) {
            p += 32;
        } else {
            #if defined(__GNUC__) || defined(__clang__)
            p += __builtin_ctz(m);
            #else
            unsigned long idx;
            _BitScanForward(&idx, m);
            p += idx;
            #endif
            break;
        }
    }
#endif

    for (; p < limit; p++)
      if ((*p & 0xFE) == 0xE8)
        break;

    {
      SizeT d = (SizeT)(p - data - pos);
      pos = (SizeT)(p - data);
      if (p >= limit)
      {
        *state = (d > 2 ? 0 : mask >> (unsigned)d);
        return pos;
      }
      if (d > 2)
        mask = 0;
      else
      {
        mask >>= (unsigned)d;
        if (mask != 0 && (mask > 4 || mask == 3 || Test86MSByte(p[(size_t)(mask >> 1) + 1])))
        {
          mask = (mask >> 1) | 4;
          pos++;
          continue;
        }
      }
    }

    if (Test86MSByte(p[4]))
    {
      UInt32 v = ((UInt32)p[4] << 24) | ((UInt32)p[3] << 16) | ((UInt32)p[2] << 8) | ((UInt32)p[1]);
      UInt32 cur = ip + (UInt32)pos;
      pos += 5;
      if (encoding)
        v += cur;
      else
        v -= cur;
      if (mask != 0)
      {
        unsigned sh = (mask & 6) << 2;
        if (Test86MSByte((Byte)(v >> sh)))
        {
          v ^= (((UInt32)0x100 << sh) - 1);
          if (encoding)
            v += cur;
          else
            v -= cur;
        }
        mask = 0;
      }
      p[1] = (Byte)v;
      p[2] = (Byte)(v >> 8);
      p[3] = (Byte)(v >> 16);
      p[4] = (Byte)(0 - ((v >> 24) & 1));
    }
    else
    {
      mask = (mask >> 1) | 4;
      pos++;
    }
  }
}
