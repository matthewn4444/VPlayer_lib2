#include "JniVideoRenderer.h"

static const char* sTag = "JniVideoRenderer";

#define _log(...) __android_log_print(ANDROID_LOG_INFO, sTag, __VA_ARGS__);

JniVideoRenderer::JniVideoRenderer() :
        mWindow(NULL),
        mSubWindow(NULL),
        mSubWindowHasData(false) {
}

JniVideoRenderer::~JniVideoRenderer() {
    release();
}

void JniVideoRenderer::onSurfaceCreated(JNIEnv *env, jobject vSurface, jobject sSurface) {
    release();

    std::lock_guard<std::mutex> lk(mMutex);
    mWindow = ANativeWindow_fromSurface(env, vSurface);
    ANativeWindow_acquire(mWindow);

    if (sSurface) {
        mSubWindow = ANativeWindow_fromSurface(env, sSurface);
        ANativeWindow_acquire(mSubWindow);
    }
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
    if (mSubWindow) {
        ANativeWindow_release(mSubWindow);
        mSubWindow = NULL;
    }
}

bool JniVideoRenderer::writeSubtitlesSeparately() {
    return mSubWindow != NULL;
}

int JniVideoRenderer::writeFrame(AVFrame* videoFrame, AVFrame* subtitleFrame) {
    if (!mWindow) return 0;
    int ret = writeFrameToWindow(videoFrame, mWindow);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to write video frame for render");
        return ret;
    }
    if (subtitleFrame) {
        if ((ret = writeFrameToWindow(subtitleFrame, mSubWindow)) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to write subtitle frame for render");
        }
        mSubWindowHasData = true;
    }
    return ret;
}

int JniVideoRenderer::renderFrame() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (mWindow) {
        ANativeWindow_unlockAndPost(mWindow);
    }
    if (mSubWindow && mSubWindowHasData) {
        ANativeWindow_unlockAndPost(mSubWindow);
    }
    mSubWindowHasData = false;
    return 0;
}

int JniVideoRenderer::writeFrameToWindow(AVFrame *frame, ANativeWindow *window) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (!window) {
        return 0;
    }
    int ret;
    ANativeWindow_Buffer buffer;
    ANativeWindow_setBuffersGeometry(window, frame->width, frame->height, WINDOW_FORMAT_RGBA_8888);
    if ((ret = ANativeWindow_lock(window, &buffer, NULL)) < 0) {
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

    int bufferLineSize = buffer.stride * 4;
    av_image_copy((uint8_t**) &buffer.bits, &bufferLineSize, (const uint8_t **) frame->data,
                  frame->linesize, (AVPixelFormat) frame->format, buffer.width, buffer.height);
    return 0;
}
