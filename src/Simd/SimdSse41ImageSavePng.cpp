/*
* Simd Library (http://ermig1979.github.io/Simd).
*
* Copyright (c) 2011-2021 Yermalayeu Ihar.
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
#include "Simd/SimdImageSave.h"
#include "Simd/SimdImageSavePng.h"
#include "Simd/SimdBase.h"
#include "Simd/SimdSsse3.h"

namespace Simd
{        
#ifdef SIMD_SSE41_ENABLE    
    namespace Sse41
    {
        static uint32_t ZlibAdler32(uint8_t* data, int size)
        {
            uint32_t lo = 1, hi = 0;
            for (int b = 0, n = (int)(size % 5552); b < size;)
            {
                for (int i = 0; i < n; ++i)
                {
                    lo += data[b + i];
                    hi += lo;
                }
                lo %= 65521;
                hi %= 65521;
                b += n;
                n = 5552;
            }
            return (hi << 16) | lo;
        }

        void ZlibCompress(uint8_t* data, int size, int quality, OutputMemoryStream& stream)
        {
            static uint16_t LEN_C[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 259 };
            static uint8_t  LEN_EB[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  0 };
            static uint16_t DIST_C[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 32768 };
            static uint8_t  DIST_EB[] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
            const int ZHASH = 16384;
            if (quality < 5)
                quality = 5;
            const int basket = quality * 2;
            Array32i hashTable(ZHASH * basket);
            memset(hashTable.data, -1, hashTable.RawSize());

            stream.Write(uint8_t(0x78));
            stream.Write(uint8_t(0x5e));
            stream.WriteBits(1, 1);
            stream.WriteBits(1, 2);

            int i = 0, j;
            while (i < size - 3)
            {
                int h = Base::ZlibHash(data + i) & (ZHASH - 1), best = 3;
                uint8_t* bestLoc = 0;
                int* hList = hashTable.data + h * basket;
                for (j = 0; hList[j] != -1 && j < basket; ++j)
                {
                    if (hList[j] > i - 32768)
                    {
                        int d = ZlibCount(data + hList[j], data + i, size - i);
                        if (d >= best)
                        {
                            best = d;
                            bestLoc = data + hList[j];
                        }
                    }
                }
                if (j == basket)
                {
                    memcpy(hList, hList + quality, quality * sizeof(int));
                    memset(hList + quality, -1, quality * sizeof(int));
                    j = quality;
                }
                hList[j] = i;

                if (bestLoc)
                {
                    h = Base::ZlibHash(data + i + 1) & (ZHASH - 1);
                    int* hList = hashTable.data + h * basket;
                    for (j = 0; hList[j] != -1 && j < basket; ++j)
                    {
                        if (hList[j] > i - 32767)
                        {
                            int e = ZlibCount(data + hList[j], data + i + 1, size - i - 1);
                            if (e > best)
                            {
                                bestLoc = NULL;
                                break;
                            }
                        }
                    }
                }

                if (bestLoc)
                {
                    int d = (int)(data + i - bestLoc);
                    assert(d <= 32767 && best <= 258);
                    for (j = 0; best > LEN_C[j + 1] - 1; ++j);
                    Base::ZlibHuff(j + 257, stream);
                    if (LEN_EB[j])
                        stream.WriteBits(best - LEN_C[j], LEN_EB[j]);
                    for (j = 0; d > DIST_C[j + 1] - 1; ++j);
                    stream.WriteBits(Base::ZlibBitRev(j, 5), 5);
                    if (DIST_EB[j])
                        stream.WriteBits(d - DIST_C[j], DIST_EB[j]);
                    i += best;
                }
                else
                {
                    Base::ZlibHuffB(data[i], stream);
                    ++i;
                }
            }
            for (; i < size; ++i)
                Base::ZlibHuffB(data[i], stream);
            Base::ZlibHuff(256, stream);
            stream.FlushBits(true);
            stream.WriteBe32(ZlibAdler32(data, size));
        }

        SIMD_INLINE uint8_t Paeth(int a, int b, int c)
        {
            int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
            if (pa <= pb && pa <= pc)
                return uint8_t(a);
            if (pb <= pc)
                return uint8_t(b);
            return uint8_t(c);
        }

        uint32_t EncodeLine(const uint8_t* src, size_t stride, size_t n, size_t size, int type, int8_t* dst)
        {
            if (type == 0)
                memcpy(dst, src, size);
            else
            {
                for (size_t i = 0; i < n; ++i)
                {
                    switch (type)
                    {
                    case 1: dst[i] = src[i]; break;
                    case 2: dst[i] = src[i] - src[i - stride]; break;
                    case 3: dst[i] = src[i] - (src[i - stride] >> 1); break;
                    case 4: dst[i] = (int8_t)(src[i] - src[i - stride]); break;
                    case 5: dst[i] = src[i]; break;
                    case 6: dst[i] = src[i]; break;
                    }
                }
                switch (type)
                {
                case 1: for (size_t i = n; i < size; ++i) dst[i] = src[i] - src[i - n]; break;
                case 2: for (size_t i = n; i < size; ++i) dst[i] = src[i] - src[i - stride]; break;
                case 3: for (size_t i = n; i < size; ++i) dst[i] = src[i] - ((src[i - n] + src[i - stride]) >> 1); break;
                case 4: for (size_t i = n; i < size; ++i) dst[i] = src[i] - Paeth(src[i - n], src[i - stride], src[i - stride - n]); break;
                case 5: for (size_t i = n; i < size; ++i) dst[i] = src[i] - (src[i - n] >> 1); break;
                case 6: for (size_t i = n; i < size; ++i) dst[i] = src[i] - src[i - n]; break;
                }
            }
            uint32_t sum = 0;
            for (size_t i = 0; i < size; ++i)
                sum += ::abs(dst[i]);
            return sum;
        }

        ImagePngSaver::ImagePngSaver(const ImageSaverParam& param)
            : Base::ImagePngSaver(param)
        {
            if (_param.format == SimdPixelFormatRgb24)
                _convert = Ssse3::BgrToRgb;
            _encode = Sse41::EncodeLine;
            _compress = Sse41::ZlibCompress;
        }
    }
#endif// SIMD_SSE41_ENABLE
}
