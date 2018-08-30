#include "YUV16to8Converter.h"
#include <android/log.h>

static const char* sTag = "Colorspace16bitConverter";

YUV16to8Converter::YUV16to8Converter(AVCodecContext *context, enum AVPixelFormat midFormat) :
        BasicYUVConverter(),
        mFrameBitDepth(0),
        mFrameBitsBigEndian(false),
#if CONVERT_16_TO_8_ASM_ENABLED
        mConversionFn(NULL),
#endif
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

#if CONVERT_16_TO_8_ASM_ENABLED
        // Get the convert function if exists for this architecture, if not supported do not
        // create the tmpFrame and use slow conversion which is faster than FFMPEG straight convert
        if (mFrameBitsBigEndian) {
            switch (mFrameBitDepth) {
                case 10: mConversionFn = &bitconv::convert10BEto8Depth; break;
                case 12: mConversionFn = &bitconv::convert12BEto8Depth; break;
                case 16: mConversionFn = &bitconv::convert16BEto8Depth; break;
                default: return;
            }
        } else {
            switch (mFrameBitDepth) {
                case 10: mConversionFn = &bitconv::convert10LEto8Depth; break;
                case 12: mConversionFn = &bitconv::convert12LEto8Depth; break;
                case 16: mConversionFn = &bitconv::convert16LEto8Depth; break;
                default: return;
            }
        }
#endif

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
#if CONVERT_16_TO_8_ASM_ENABLED
    mConversionFn = NULL;
#endif
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
    const size_t width = (size_t) srcFrame->width;
    const size_t height = (size_t) srcFrame->height;
    const enum AVPixelFormat srcFormat = (enum AVPixelFormat) srcFrame->format;
    const enum AVPixelFormat dstFormat = (enum AVPixelFormat) dstFrame->format;
    const AVPixFmtDescriptor *srcDesc = av_pix_fmt_desc_get(srcFormat);
    const AVPixFmtDescriptor *dstDesc = av_pix_fmt_desc_get(dstFormat);

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
    size_t srcStride = (size_t) srcFrame->linesize[0];
    size_t dstStride = (size_t) dstFrame->linesize[0];
    reduce16BitChannelDepth(src, dst, srcStride, dstStride, width, height);

    // Convert U
    src = (uint16_t *) srcFrame->data[1];
    dst = dstFrame->data[1];
    srcStride = (size_t)  srcFrame->linesize[1];
    dstStride = (size_t)  dstFrame->linesize[1];
    reduce16BitChannelDepth(src, dst, srcStride, dstStride, width >> srcDesc->log2_chroma_w,
                            height >> srcDesc->log2_chroma_h);

    // Convert V
    src = (uint16_t *) srcFrame->data[2];
    dst = dstFrame->data[2];
    srcStride = (size_t) srcFrame->linesize[2];
    dstStride = (size_t) dstFrame->linesize[2];
    reduce16BitChannelDepth(src, dst, srcStride, dstStride, width >> srcDesc->log2_chroma_w,
                            height >> srcDesc->log2_chroma_h);
    return true;
}

void YUV16to8Converter::reduce16BitChannelDepth(const uint16_t *src, uint8_t *dst,
                                                size_t srcStride, size_t dstStride, size_t width,
                                                size_t height) {
#if CONVERT_16_TO_8_ASM_ENABLED
    if (mConversionFn) {
        mConversionFn(src, dst, srcStride, dstStride, width, height);
        return;
    }
#endif
    // Slow software solution to convert 10-16bit to 8bit, same chroma sampling
    const size_t scaleFactor = (size_t) mFrameBitDepth - 8;
    for (size_t r = height; r > 0; --r) {
        for (size_t c = 0; c < width; ++c) {
            int16_t d = src[c];
            if (mFrameBitsBigEndian) {
                d = (d >> 8) | (d << 8);
            }
            dst[c] = (uint8_t) (d >> scaleFactor);
        }
        // Divide source stride by 2 because it is 16 bit container converted to 8 bit
        src += srcStride / 2;
        dst += dstStride;
    }
}
