#ifndef __CONVERT_H__
#define __CONVERT_H__

#include <cstdio>
#include <math.h>

#if __aarch64__

#define CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, __EXTRA__) \
    size_t srcStrideOffset = (size_t) (_srcStride - ceil(_width / 16.0) * 32);  \
    size_t dstStrideOffset = (size_t) (_dstStride - ceil(_width / 16.0) * 16);  \
    asm volatile (                                                             \
        /* For loop row */                                                      \
        "1:                                                                 \n" \
                                                                                \
        /* Set the width to count down per row */                               \
        "mov            x0, %2                                              \n" \
                                                                                \
        /* For loop batch column */                                             \
        "2:                                                                 \n" \
                                                                                \
        /* Load the source data */                                              \
        "ld1            {v0.8h}, [%0], #16                                  \n" \
                                                                                \
        __EXTRA__                                                               \
                                                                                \
        /* Convert from 16bit to 8bit: eg. v = v >> 2 or v = v / 4 */           \
        "uqshrn         v0.8b, v0.8h, #"#_BITS"                             \n" \
                                                                                \
        /* Write to destination */                                              \
        "st1            {v0.8b}, [%1], #8                                   \n" \
                                                                                \
        /* End loop, x = width; x > 0; x -= 8 */                                \
        "subs           x0, x0, #8                                          \n" \
        "b.gt           2b                                                  \n" \
                                                                                \
        /* End loop,y = height; y > 0; --y */                                   \
        /* Offset the src/dst pointers if extra padding compared to width */    \
        "add            %0, %0, %4                                          \n" \
        "add            %1, %1, %5                                          \n" \
        "subs           %3, %3, #1                                          \n" \
        "b.gt           1b                                                  \n" \
        :                                                                       \
        :   "r" (_src),                                                         \
            "r" (_dst),                                                         \
            "r" (_width),                                                       \
            "r" (_height),                                                      \
            "r" (srcStrideOffset),                                              \
            "r" (dstStrideOffset)                                               \
        :   "memory", "cc", "x0", "v0"                                          \
    )

#define CONVERT_16BE_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS) \
    CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, \
        "rev16          v0.16b, v0.16b /* big endian conversion */          \n")

#elif __ARM_NEON__

#define CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, __EXTRA__) \
    size_t srcStrideOffset = (size_t) (_srcStride - ceil(_width / 16.0) * 32);  \
    size_t dstStrideOffset = (size_t) (_dstStride - ceil(_width / 16.0) * 16);  \
    asm volatile (                                                             \
        /* For loop row */                                                      \
        "1:                                                                 \n" \
                                                                                \
        /* Set the width to count down per row */                               \
        "mov            r0, %2                                              \n" \
                                                                                \
        /* For loop batch column */                                             \
        "2:                                                                 \n" \
                                                                                \
        /* Load the source data */                                              \
        "vld1.u16       {d0}, [%0]!                                         \n" \
        "vld1.u16       {d2}, [%0]!                                         \n" \
                                                                                \
        __EXTRA__                                                               \
                                                                                \
        /* Convert from 16bit to 8bit: eg. v = v >> 2 or v = v / 4 */           \
        "vqshrn.u16     d0, q0, #"#_BITS"                                   \n" \
        "vqshrn.u16     d1, q1, #"#_BITS"                                   \n" \
                                                                                \
        /* Merge first 4 bytes of one and last 4 bytes of the other */          \
        "vtrn.32        d0, d1                                              \n" \
                                                                                \
        /* Write to destination */                                              \
        "vst1.8         {d0}, [%1]!                                         \n" \
                                                                                \
        /* End loop, x = width; x > 0; x -= 8 */                                \
        "subs           r0, r0, #8                                          \n" \
        "bgt            2b                                                  \n" \
                                                                                \
        /* End loop,y = height; y > 0; --y */                                   \
        /* Offset the src/dst pointers if extra padding compared to width */    \
        "add            %0, %0, %4                                          \n" \
        "add            %1, %1, %5                                          \n" \
        "subs           %3, %3, #1                                          \n" \
        "bgt            1b                                                  \n" \
        :                                                                       \
        :   "r" (_src),                                                         \
            "r" (_dst),                                                         \
            "r" (_width),                                                       \
            "r" (_height),                                                      \
            "r" (srcStrideOffset),                                              \
            "r" (dstStrideOffset)                                               \
        :   "memory", "cc", "r0", "q0", "q1"                                    \
    );

#define CONVERT_16BE_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS) \
    CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, \
        /* If big endian, flip it */                                                \
        "vrev16.8       d0, d0  /* big endian conversion */                 \n" \
        "vrev16.8       d2, d2                                              \n")

#elif __x86_64__

#define CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, __EXTRA__) \
    size_t srcStrideOffset = (size_t) (_srcStride - ceil(_width / 16.0) * 32);  \
    size_t dstStrideOffset = (size_t) (_dstStride - ceil(_width / 16.0) * 16);  \
    asm volatile (                                                             \
        /* All 0s for packing */                                                \
        "xorps          %%xmm0, %%xmm0                                      \n" \
                                                                                \
        /* For loop row */                                                      \
        "1:                                                                 \n" \
                                                                                \
        /* Set the width to count down per row */                               \
        "mov            %2, %%rax                                           \n" \
                                                                                \
        /* For loop batch column */                                             \
        "2:                                                                 \n" \
                                                                                \
        /* Load the source data */                                              \
        "movdqu         (%0), %%xmm1                                        \n" \
        "movdqu         16(%0), %%xmm2                                      \n" \
        "add            $32, %0                                             \n" \
                                                                                \
        __EXTRA__                                                               \
                                                                                \
        /* Shift each 16 bit to the right to convert */                         \
        "psrlw          $"#_BITS", %%xmm1                                   \n" \
        "psrlw          $"#_BITS", %%xmm2                                   \n" \
                                                                                \
        /* Merge both 16bit vectors into one 8bit vector */                     \
        "packuswb       %%xmm0, %%xmm1                                      \n" \
        "packuswb       %%xmm0, %%xmm2\n"                                       \
        "unpcklpd       %%xmm2, %%xmm1\n"                                       \
                                                                                \
        /* Write to destination */                                              \
        "movdqu         %%xmm1, (%1)                                        \n" \
        "add            $16, %1                                             \n" \
                                                                                \
        /* End loop, x = width; x >= 0 ; x -= 16 */                             \
        "sub            $16, %%rax                                          \n" \
        "jg             2b                                                  \n" \
                                                                                \
        /* End loop, y = height; y >= 0; --y */                                 \
        "add            %4, %0                                              \n" \
        "add            %5, %1                                              \n" \
        "sub            $1, %3                                              \n" \
        "jg             1b                                                  \n" \
        :   "+r" (_src),                                                        \
            "+r" (_dst),                                                        \
            "+r" (_width),                                                      \
            "+r" (_height),                                                     \
            "+r" (srcStrideOffset),                                             \
            "+r" (dstStrideOffset)                                              \
        :                                                                       \
        :   "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "rax"               \
    );

#define CONVERT_16BE_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS) \
    CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, \
        "movdqu         %%xmm1, %%xmm3                                      \n" \
        "psrlw          $0x8, %%xmm1                                        \n" \
        "psllw          $0x8, %%xmm3                                        \n" \
        "orpd           %%xmm3, %%xmm1                                      \n "\
                                                                                \
        "movdqu         %%xmm2, %%xmm3                                      \n" \
        "psrlw          $0x8, %%xmm2                                        \n" \
        "psllw          $0x8, %%xmm3                                        \n" \
        "orpd           %%xmm3, %%xmm2                                      \n")

#elif defined(__i386__) && defined(__SSE2__)

#define CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, __EXTRA__) \
    size_t dstStrideOffset = (size_t) (_dstStride - ceil(_srcStride / 2));      \
    asm volatile (                                                             \
        "push           %%ebp                                               \n" \
                                                                                \
        /* All 0s for packing */                                                \
        "xorps          %%xmm0, %%xmm0                                      \n" \
                                                                                \
        /* For loop row */                                                      \
        "1:                                                                 \n" \
                                                                                \
        /* Set the width to count down per row */                               \
        "mov %2,        %%ebp                                               \n" \
                                                                                \
        /* For loop batch column */                                             \
        "2:                                                                 \n" \
                                                                                \
        /* Load the source data */                                              \
        "movdqu         (%0), %%xmm1                                        \n" \
        "movdqu         16(%0), %%xmm2                                      \n" \
        "add            $32, %0                                             \n" \
                                                                                \
        __EXTRA__                                                               \
                                                                                \
        /* Shift each 16 bit to the right to convert */                         \
        "psrlw          $"#_BITS", %%xmm1                                   \n" \
        "psrlw          $"#_BITS", %%xmm2                                   \n" \
                                                                                \
        /* Merge both 16bit vectors into one 8bit vector */                     \
        "packuswb       %%xmm0, %%xmm1                                      \n" \
        "packuswb       %%xmm0, %%xmm2                                      \n" \
        "unpcklpd       %%xmm2, %%xmm1                                      \n" \
                                                                                \
        /* Write to destination */                                              \
        "movdqu         %%xmm1, (%1)                                        \n" \
        "add            $16, %1                                             \n" \
                                                                                \
        /* End loop, x = srcStride; x >= 0 ; x -= 32 */                         \
        "sub            $32, %%ebp                                          \n" \
        "jg             2b                                                  \n" \
                                                                                \
        /* End loop, y = height; y >= 0; --y */                                 \
        "add %4,        %1                                                  \n" \
        "sub $1,        %3                                                  \n" \
        "jg             1b                                                  \n" \
                                                                                \
        "pop            %%ebp                                               \n" \
        :   "+r" (_src),                                                        \
            "+r" (_dst),                                                        \
            "+r" (_srcStride),                                                  \
            "+r" (_height),                                                     \
            "+r" (dstStrideOffset)                                              \
        :                                                                       \
        :   "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3"                      \
    );

#define CONVERT_16BE_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS) \
    CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, \
        "movdqu         %%xmm1, %%xmm3                                      \n" \
        "psrlw          $0x8, %%xmm1                                        \n" \
        "psllw          $0x8, %%xmm3                                        \n" \
        "orpd           %%xmm3, %%xmm1                                      \n" \
                                                                                \
        "movdqu         %%xmm2, %%xmm3                                      \n" \
        "psrlw          $0x8, %%xmm2                                        \n" \
        "psllw          $0x8, %%xmm3                                        \n" \
        "orpd           %%xmm3, %%xmm2                                      \n")

#elif __SSE3__

// This section may never run, figure out a better way to catch if asm does not work out
// Intrinics is almost twice slower than asm above but for future arch might be faster
#include <immintrin.h>
#define CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, __EXTRA__) \
    static const __m128i zeros = _mm_setzero_si128();                                  \
    const size_t srcStrideOffset = (size_t) (_srcStride - ceil(_width / 16.0) * 32);    \
    const size_t dstStrideOffset = (size_t) (_dstStride - ceil(_width / 16.0) * 16);    \
                                                                                        \
     __m128i mdata, mdata2;                                                             \
    for (size_t y = _height; y > 0; --y) {                                              \
        for (size_t x = _srcStride; x > 0; x -= 32) {                                   \
            mdata = _mm_loadu_si128((const __m128i *)_src);                             \
            mdata2 = _mm_loadu_si128((const __m128i *)(_src + 8));                      \
                                                                                        \
            __EXTRA__                                                                   \
                                                                                        \
            /* Shift each 16 bit to the right to convert */                             \
            mdata = _mm_packus_epi16(_mm_srli_epi16(mdata, _BITS), zeros);              \
            mdata2 = _mm_packus_epi16(_mm_srli_epi16(mdata2, _BITS), zeros);            \
                                                                                        \
            /* Pack upper and lower then write to memory */                             \
            _mm_storeu_si128(reinterpret_cast<__m128i *>(_dst),                        \
                             static_cast<__m128i>(_mm_unpacklo_pd(mdata, mdata2)));    \
            _src += 16;                                                                 \
            _dst += 16;                                                                 \
        }                                                                               \
        _src += srcStrideOffset;                                                        \
        _dst += dstStrideOffset;                                                        \
    }

#define CONVERT_16BE_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS) \
    CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS,    \
            static const __m128i mask = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10,        \
                                                     11, 4, 5, 6, 7, 0, 1, 2, 3);      \
            mdata = _mm_shuffle_epi8(mdata, mask);                                     \
            mdata2 = _mm_shuffle_epi8(mdata2, mask);                                   )

#endif

#if defined(CONVERT_16_TO_8_ASM)
#define CONVERT_16_TO_8_ASM_ENABLED 1

// Default for little endian
#define CONVERT_16LE_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS) \
    CONVERT_16_TO_8_ASM(_src, _dst, _srcStride, _dstStride, _width, _height, _BITS, /* empty */)

namespace bitconv {
    typedef void (*conv16_8_func)(const uint16_t*, uint8_t*, size_t, size_t, size_t, size_t);

    void convert10LEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride,
                                    size_t dstStride, size_t width, size_t height);

    void convert10BEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride,
                                    size_t dstStride, size_t width, size_t height);

    void convert12LEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride,
                                    size_t dstStride, size_t width, size_t height);

    void convert12BEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride,
                                    size_t dstStride, size_t width, size_t height);

    void convert16LEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride,
                                    size_t dstStride, size_t width, size_t height);

    void convert16BEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride,
                                    size_t dstStride, size_t width, size_t height);
}
#else
#undef CONVERT_16_TO_8_ASM_ENABLED
#endif

#endif // __CONVERT_H__
