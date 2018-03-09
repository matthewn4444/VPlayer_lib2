#include "JniVideoRenderer.h"

static const char* sTag = "JniVideoRenderer";

#define _log(...) __android_log_print(ANDROID_LOG_INFO, sTag, __VA_ARGS__);

JniVideoRenderer::JniVideoRenderer() :
aaa(0),
        mWindow(NULL),
        mBufferFrame(NULL),
        mSwsContext(NULL) {
    mBufferFrame = av_frame_alloc();
}

JniVideoRenderer::~JniVideoRenderer() {
    release();
    if (mBufferFrame) {
        av_frame_free(&mBufferFrame);
        mBufferFrame = NULL;
    }
    if (mSwsContext) {
        sws_freeContext(mSwsContext);
        mSwsContext = NULL;
    }
}

void JniVideoRenderer::onSurfaceCreated(JNIEnv *env, jobject surface) {
    release();

    std::lock_guard<std::mutex> lk(mMutex);
    mWindow = ANativeWindow_fromSurface(env, surface);
    ANativeWindow_acquire(mWindow);
}

void JniVideoRenderer::onSurfaceDestroyed() {
    release();
}

void JniVideoRenderer::release() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (mWindow) {
        _log("release window")
        ANativeWindow_release(mWindow);
        mWindow = NULL;
    }
}

int JniVideoRenderer::renderFrame(AVFrame *frame) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (!mWindow) {
        return 0;
    }

    int ret;
    ANativeWindow_Buffer buffer;

    ANativeWindow_setBuffersGeometry(mWindow, frame->width, frame->height,
                                     WINDOW_FORMAT_RGBA_8888);
    if ((ret = ANativeWindow_lock(mWindow, &buffer, NULL)) < 0) {
        return ret;
    }

    enum AVPixelFormat outFormat = AV_PIX_FMT_NONE;
    switch (buffer.format) {
        case WINDOW_FORMAT_RGBA_8888:
            outFormat = AV_PIX_FMT_RGBA;
            break;
        case WINDOW_FORMAT_RGBX_8888:
            outFormat = AV_PIX_FMT_RGB0;
            break;
        case WINDOW_FORMAT_RGB_565:
            outFormat = AV_PIX_FMT_RGB565;
            break;
        default:
            break;
    }

    if (outFormat != AV_PIX_FMT_RGBA) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Format not supported %s",
                            av_get_pix_fmt_name(outFormat));
        return AVERROR(EOPNOTSUPP);
    }

    // Check buffer
    // TODO does this make sense?
    if ((ret = av_image_fill_arrays(mBufferFrame->data, mBufferFrame->linesize,
                                    (uint8_t *) buffer.bits, outFormat, frame->width,
                                    frame->height, 1)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot fill video buffer array");
        return ret;
    }
    mBufferFrame->data[0] = (uint8_t *) buffer.bits;
    mBufferFrame->linesize[0] = buffer.stride * 4;
//    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Buffer: width = %d, height = %d, stride = %d",
//                        buffer.width, buffer.height, buffer.stride);

    // TODO use libyuv and benchmark time
    mSwsContext = sws_getCachedContext(mSwsContext, frame->width, frame->height,
                                       (enum AVPixelFormat) frame->format, frame->width,
                                       frame->height, outFormat, SWS_BICUBIC, NULL, NULL, NULL);
    if (!mSwsContext) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate conversion context");
        return AVERROR(EINVAL);
    }
    if ((ret = sws_scale(mSwsContext, (const uint8_t *const *) frame->data, frame->linesize, 0,
                         frame->height, mBufferFrame->data, mBufferFrame->linesize)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot convert frame to surface");
        return ret;
    }

    ANativeWindow_unlockAndPost(mWindow);
    return 0;
}
