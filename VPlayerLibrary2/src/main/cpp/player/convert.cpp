#include "convert.h"

namespace bitconv {

#if defined(CONVERT_16_TO_8_ASM_ENABLED)

    void convert10BEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride, size_t dstStride,
                             size_t width, size_t height) {
        CONVERT_16BE_TO_8_ASM(src, dst, srcStride, dstStride, width, height, 2);
    }

    void convert10LEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride, size_t dstStride,
                             size_t width, size_t height) {
        CONVERT_16LE_TO_8_ASM(src, dst, srcStride, dstStride, width, height, 2);
    }

    void convert12LEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride, size_t dstStride,
                             size_t width, size_t height) {
        CONVERT_16LE_TO_8_ASM(src, dst, srcStride, dstStride, width, height, 4);
    }

    void convert12BEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride, size_t dstStride,
                             size_t width, size_t height) {
        CONVERT_16BE_TO_8_ASM(src, dst, srcStride, dstStride, width, height, 4);
    }

    void convert16LEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride, size_t dstStride,
                             size_t width, size_t height) {
        CONVERT_16LE_TO_8_ASM(src, dst, srcStride, dstStride, width, height, 8);
    }

    void convert16BEto8Depth(const uint16_t *src, uint8_t *dst, size_t srcStride, size_t dstStride,
                             size_t width, size_t height) {
        CONVERT_16BE_TO_8_ASM(src, dst, srcStride, dstStride, width, height, 8);
    }

#endif
}
