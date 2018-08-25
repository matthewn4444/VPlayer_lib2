#include "YUV16to8Converter.h"
#include <android/log.h>

static const char* sTag = "Colorspace16bitConverter";

YUV16to8Converter::YUV16to8Converter(AVCodecContext *context, enum AVPixelFormat midFormat) :
        BasicYUVConverter(),
        mFrameBitDepth(0),
        mFrameBitsBigEndian(false),
        mTmpFrame(NULL) {
    if (midFormat != AV_PIX_FMT_YUV420P && midFormat != AV_PIX_FMT_YUV422P
            && midFormat != AV_PIX_FMT_YUV444P) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Invalid mid format, will use slow conversion");
        return;
    }

    const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(context->pix_fmt);
    mFrameBitDepth = descriptor->comp->depth;
    mFrameBitsBigEndian = (descriptor->flags & AV_PIX_FMT_FLAG_BE) != 0;

    // If the bitdepth is higher than 8 bit, then need a temp frame to convert to 8bit before rgba
    if (mFrameBitDepth > 8) {
        mTmpFrame = av_frame_alloc();
        mTmpFrame->format = midFormat;

        // Align the data that would be optimal for neon acceleration, 16 bytes for arm64
        if (av_image_alloc(mTmpFrame->data, mTmpFrame->linesize, context->width,
                           context->height, midFormat, 16) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, sTag,
                                "Unable to allocate temporary frame for bit depth conversion");
        }
    }
}

YUV16to8Converter::~YUV16to8Converter() {
    if (mTmpFrame) {
        av_frame_unref(mTmpFrame);
        av_frame_free(&mTmpFrame);
        mTmpFrame = NULL;
    }
}

int YUV16to8Converter::convert(AVFrame *srcFrame, AVFrame *dstFrame) {
    if (mTmpFrame) {
        mTmpFrame->width = srcFrame->width;
        mTmpFrame->height = srcFrame->height;

        if (reduceYUV16to8bit(srcFrame, mTmpFrame)) {
            srcFrame = mTmpFrame;
        }
    }
    return BasicYUVConverter::convert(srcFrame, dstFrame);
}

bool YUV16to8Converter::reduceYUV16to8bit(AVFrame *srcFrame, AVFrame *dstFrame) {
    const int width = srcFrame->width;
    const int height = srcFrame->height;
    const enum AVPixelFormat srcFormat = (enum AVPixelFormat) srcFrame->format;
    const enum AVPixelFormat dstFormat = (enum AVPixelFormat) dstFrame->format;
    const AVPixFmtDescriptor *srcDesc = av_pix_fmt_desc_get(srcFormat);
    const AVPixFmtDescriptor *dstDesc = av_pix_fmt_desc_get(dstFormat);
    const int scaleFactor = mFrameBitDepth - 8;

    if (srcDesc->log2_chroma_h != dstDesc->log2_chroma_h
        || srcDesc->log2_chroma_w != dstDesc->log2_chroma_w) {
        // Not same chroma sampling
        __android_log_print(ANDROID_LOG_WARN, sTag, "Unable to convert, the src and dst pixel "
                "formats have different structures [%d %d vs %d %d].", srcDesc->log2_chroma_h,
                            dstDesc->log2_chroma_h, srcDesc->log2_chroma_w, dstDesc->log2_chroma_w);
        return false;
    }

    // Convert Y
    uint16_t * src = (uint16_t *) srcFrame->data[0];
    uint8_t * dst = dstFrame->data[0];
    int srcStride = srcFrame->linesize[0];
    int dstStride = dstFrame->linesize[0];
    reduce16BitChannelDepth(src, dst, srcStride, dstStride, width, height, mFrameBitsBigEndian,
                            scaleFactor);

    // Convert U
    src = (uint16_t *) srcFrame->data[1];
    dst = dstFrame->data[1];
    srcStride =  srcFrame->linesize[1] >> srcDesc->log2_chroma_h;
    dstStride = dstFrame->linesize[1] >> srcDesc->log2_chroma_h;
    reduce16BitChannelDepth(src, dst, srcStride, dstStride, width >> srcDesc->log2_chroma_w,
                            height, mFrameBitsBigEndian, scaleFactor);

    // Convert V
    src = (uint16_t *) srcFrame->data[2];
    dst = dstFrame->data[2];
    srcStride =  srcFrame->linesize[2] >> srcDesc->log2_chroma_h;
    dstStride = dstFrame->linesize[2] >> srcDesc->log2_chroma_h;
    reduce16BitChannelDepth(src, dst, srcStride, dstStride, width >> srcDesc->log2_chroma_w,
                            height, mFrameBitsBigEndian, scaleFactor);
    return true;
}

void YUV16to8Converter::reduce16BitChannelDepth(uint16_t *src, uint8_t *dst, int srcStride,
                                                int dstStride, int width, int height,
                                                bool isBigEndian, int scaleFactor) {
#if __aarch64__
    asm (
        "mov x0, %x[src]\n"
        "mov x1, %x[dst]\n"
        "mov x2, %x[width]\n"
        "mov x3, %x[height]\n"
        "mov x4, %x[srcStride]\n"
        "mov x5, %x[dstStride]\n"
        "mov x6, %x[isBigEndian]\n"
        "mov x7, %x[scaleFactor]\n"

        // For loop
        "mov x9, #0\n"
        "yloop:\n"

        "mov x10, #0\n"
        "madd x11, x9, x4, x0\n"                    // Source: src = y * srcStride + src
        "madd x12, x9, x5, x1\n"                    // Dest:   dst = y * dstStride + dst

        "xloop:\n"

        // Load the source data
        "ld1 {v0.8h}, [x11], #16\n"                 // Source pointer

        // If big endian, flip it
        "cmp x6, #1\n"
        "bne scale_10bit\n"
        "rev16 v0.16b, v0.16b\n"

        // Convert each channel of the data into 8bit, check by scaling factor to convert
        // otherwise do nothing, empty output

        // 10 bit: scale by 2
        "scale_10bit:\n"
        "cmp x7, #2\n"
        "bne scale_12bit\n"
        "uqshrn v1.8b, v0.8h, #2\n"                 // v = v >> 2 or v = v / 4
        "b write\n"

        // 12 bit: scale by 4
        "scale_12bit:\n"
        "cmp x7, #4\n"
        "bne scale_16bit\n"
        "uqshrn v1.8b, v0.8h, #4\n"                 // v = v >> 4 or v = v / 16
        "b write\n"

        // 16 bit: scale by 8
        "scale_16bit:\n"
        "uqshrn v1.8b, v0.8h, #8\n"                 // v = v >> 8 or v = v / 256


        // Write to destination
        "write:\n"
        "st1 {v1.8b}, [x12], #8\n"

        // End loop, x < width; x += 8
        "add x10, x10, #8\n"
        "cmp x2, x10\n"
        "b.gt xloop\n"

        // End loop, y < height; y++
        "add x9, x9, #1\n"
        "cmp x3, x9\n"
        "b.gt yloop\n"
    :
    : [src] "r" (src), [dst] "r" (dst),
        [width] "r" (width), [height] "r" (height), [srcStride] "r" (srcStride),
        [dstStride] "r" (dstStride), [isBigEndian] "r" (isBigEndian ? 1 : 0),
        [scaleFactor] "r" (scaleFactor)
    : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"
    );
#elif __ARM_NEON__
    // Limit the number of inputs into inline assembly by storing the data and loading input vars
    int16_t dimensions[] = {(int16_t) width, (int16_t) height,
                            (int16_t) srcStride, (int16_t) dstStride};
    asm  (
        "push {r4-r11}\n"
        "mov r0, %[src]\n"
        "mov r1, %[dst]\n"
        "mov r2, %[dimensions]\n"
        "mov r6, %[isBigEndian]\n"
        "mov r7, %[scaleFactor]\n"

        // Extract the data for the input dimensions
        "vld1.u16 {d0}, [r2]!\n"
        "vmov.u16 r2, d0[0]\n"
        "vmov.u16 r3, d0[1]\n"
        "vmov.u16 r4, d0[2]\n"
        "vmov.u16 r5, d0[3]\n"

        // For loop
        "mov r8, #0\n"
        "yloop:\n"

        "mov r9, #0\n"
        "mla r10, r8, r4, r0\n"                      // Source: src = y * srcStride + src
        "mla r11, r8, r5, r1\n"                      // Dest:   dst = y * dstStride + dst

        "xloop:\n"

        // Load the source
        "vld1.u16 {d0}, [r10]!\n"
        "vld1.u16 {d2}, [r10]!\n"

        // If big endian, flip it
        "cmp r6, #1\n"
        "bne scale_10bit\n"
        "vrev16.8 d0, d0\n"
        "vrev16.8 d2, d2\n"

        // Convert each channel of the data into 8bit, check by scaling factor to convert
        // otherwise do nothing, empty output

        // 10 bit: scale by 2
        "scale_10bit:\n"
        "cmp r7, #2\n"
        "bne scale_12bit\n"
        "vqshrn.u16 d0, q0, #2\n"                 // v = v >> 2 or v = v / 4
        "vqshrn.u16 d1, q1, #2\n"
        "b continue\n"

        // 12 bit: scale by 4
        "scale_12bit:\n"
        "cmp r7, #4\n"
        "bne scale_16bit\n"
        "vqshrn.u16 d0, q0, #4\n"                 // v = v >> 4 or v = v / 16
        "vqshrn.u16 d1, q1, #4\n"
        "b continue\n"

        // 16 bit: scale by 8
        "scale_16bit:\n"
        "vqshrn.u16 d0, q0, #8\n"                 // v = v >> 8 or v = v / 256
        "vqshrn.u16 d1, q1, #8\n"


        // Merge by transpose the first 4 bytes of one and the last 4 bytes of the other
        "continue:\n"
        "vtrn.32 d0, d1\n"

        // Write to destination and move buffer
        "vst1.8 {d0}, [r11]!\n"

        // End loop, x < width; x += 8
        "add r9, r9, #8\n"
        "cmp r2, r9\n"
        "bgt xloop\n"

        //  End loop, y < height; y++
        "add r8, r8, #1\n"
        "cmp r3, r8\n"
        "bgt yloop\n"

        "pop {r4-r11}\n"
    :
    : [src] "r" (src), [dst] "r" (dst), [dimensions] "r" (dimensions),
        [isBigEndian] "r" (isBigEndian ? 1 : 0), [scaleFactor] "r" ((int64_t) scaleFactor)
    : "r0", "r1", "r2", "r3", "r4"
    );
#else
    // Slow software solution to convert 10-16bit to 8bit, same chroma sampling
    for (int r = 0; r < height; r++) {
        for (int c = 0; c < width; c++) {
            int16_t d = src[c];
            if (isBigEndian) {
                d = (d >> 8) | (d << 8);
            }
            dst[c] = (uint8_t) ((d >> scaleFactor) & 0xFF);
        }
        // Divide source stride by 2 because it is 16 bit container converted to 8 bit
        src += srcStride / 2;
        dst += dstStride;
    }
#endif
}
