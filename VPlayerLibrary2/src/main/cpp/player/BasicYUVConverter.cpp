#include "BasicYUVConverter.h"
#include <android/log.h>
#include <libyuv.h>

static const char* sTag = "BasicColorspaceConverter";

BasicYUVConverter::BasicYUVConverter() :
        mSwsContext(NULL) {
}

BasicYUVConverter::~BasicYUVConverter() {
    if (mSwsContext) {
        sws_freeContext(mSwsContext);
        mSwsContext = NULL;
    }
}

int BasicYUVConverter::convert(AVFrame *src, AVFrame *dst) {
    int ret = 0;
    if (src->format == AV_PIX_FMT_YUV444P) {
        // For some reason FFMPEG has not written their yuv444 implementation in assembly...
        libyuv::I444ToARGB(src->data[0], src->linesize[0],
                           src->data[2], src->linesize[2],
                           src->data[1], src->linesize[1],
                           dst->data[0], dst->linesize[0],
                           src->width, src->height);
    } else {
        mSwsContext = sws_getCachedContext(mSwsContext, src->width, src->height,
                                           (enum AVPixelFormat) src->format, src->width,
                                           src->height, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL,
                                           NULL);
        if (!mSwsContext) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate conversion context");
            return AVERROR(EINVAL);
        }
        if ((ret = sws_scale(mSwsContext, (const uint8_t *const *) src->data, src->linesize, 0,
                             src->height, dst->data, dst->linesize)) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot convert frame");
        }
    }
    return ret;
}
