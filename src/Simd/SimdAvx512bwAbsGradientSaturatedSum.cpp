/*
* Simd Library (http://ermig1979.github.io/Simd).
*
* Copyright (c) 2011-2017 Yermalayeu Ihar.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy 
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
* copies of the Software, and to permit persons to whom the Software is 
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in 
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include "Simd/SimdMemory.h"
#include "Simd/SimdMath.h"
#include "Simd/SimdStore.h"

namespace Simd
{
#ifdef SIMD_AVX512BW_ENABLE    
	namespace Avx512bw
	{
		template<bool align, bool mask> SIMD_INLINE void AbsGradientSaturatedSum(const uint8_t * src, size_t stride, uint8_t * dst, __mmask64 m = -1)
		{
			const __m512i s10 = Load<false, mask>(src - 1, m);
			const __m512i s12 = Load<false, mask>(src + 1, m);
			const __m512i s01 = Load<align, mask>(src - stride, m);
			const __m512i s21 = Load<align, mask>(src + stride, m);
			const __m512i dx = AbsDifferenceU8(s10, s12);
			const __m512i dy = AbsDifferenceU8(s01, s21);
			Store<align, mask>(dst, _mm512_adds_epu8(dx, dy), m);
		}

		template<bool align> void AbsGradientSaturatedSum(const uint8_t * src, size_t srcStride, size_t width, size_t height, uint8_t * dst, size_t dstStride)
		{
			size_t alignedWidth = AlignLo(width, A);
			__mmask64 tailMask = __mmask64(-1) >> (A + alignedWidth - width);
			memset(dst, 0, width);
			src += srcStride;
			dst += dstStride;
			for (size_t row = 2; row < height; ++row)
			{
				size_t col = 0;
				for (; col < alignedWidth; col += A)
					AbsGradientSaturatedSum<align, false>(src + col, srcStride, dst + col);
				if(col < width)
					AbsGradientSaturatedSum<align, true>(src + col, srcStride, dst + col, tailMask);

				dst[0] = 0;
				dst[width - 1] = 0;

				src += srcStride;
				dst += dstStride;
			}
			memset(dst, 0, width);
		}

		void AbsGradientSaturatedSum(const uint8_t * src, size_t srcStride, size_t width, size_t height, uint8_t * dst, size_t dstStride)
		{
			if(Aligned(src) && Aligned(srcStride) && Aligned(dst) && Aligned(dstStride))
				AbsGradientSaturatedSum<true>(src, srcStride, width, height, dst, dstStride);
			else
				AbsGradientSaturatedSum<false>(src, srcStride, width, height, dst, dstStride);
		}
	}
#endif// SIMD_AVX512BW_ENABLE
}
