#include "JniVideoRenderer.h"

static const char* sTag = "JniVideoRenderer";

#define _log(...) __android_log_print(ANDROID_LOG_INFO, sTag, __VA_ARGS__);

JniVideoRenderer::JniVideoRenderer() :
        mWindow(NULL) {
}

JniVideoRenderer::~JniVideoRenderer() {
    release();
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
    ANativeWindow_Buffer buffer;            // TODO can this be reused?
    ANativeWindow_setBuffersGeometry(mWindow, frame->width, frame->height, WINDOW_FORMAT_RGBA_8888);
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

    // TODO see if we can do this in a vendering thread earlier (just before rendering time)
    int bufferLineSize = buffer.stride * 4;
    av_image_copy((uint8_t**) &buffer.bits, &bufferLineSize, (const uint8_t **) frame->data,
                  frame->linesize, (AVPixelFormat) frame->format, buffer.width, buffer.height);

    ANativeWindow_unlockAndPost(mWindow);
    return 0;
}
