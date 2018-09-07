#include "JniVideoRenderer.h"

static const char* sTag = "JniVideoRenderer";

#define _log(...) __android_log_print(ANDROID_LOG_INFO, sTag, __VA_ARGS__);

static void copyRGBABuffer(const ANativeWindow_Buffer& dst, const ANativeWindow_Buffer& src) {
    int stride = dst.stride * 4;
    av_image_copy((uint8_t **) &dst.bits, &stride, (const uint8_t **) &src.bits, &stride,
                  AV_PIX_FMT_RGBA, src.width, src.height);
}

JniVideoRenderer::JniVideoRenderer() :
        mWindow(NULL),
        mSubWindow(NULL),
        mSubWindowHasData(false) {
    mWindowBuffer.width = mWindowBuffer.height = 0;
    mSubWindowBuffer.width = mSubWindowBuffer.height = 0;
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
    mSubWindowHasData = false;
}

bool JniVideoRenderer::writeSubtitlesSeparately() {
    return mSubWindow != NULL;
}

int JniVideoRenderer::writeFrame(AVFrame* videoFrame, AVFrame* subtitleFrame) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (!mWindow) return 0;
    int ret = writeFrameToWindow(videoFrame, mWindow, mWindowBuffer);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to write video frame for render");
        return ret;
    }

    if (mSubWindow && subtitleFrame) {
        if ((ret = writeFrameToWindow(subtitleFrame, mSubWindow, mSubWindowBuffer)) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to write sub frame for render");
        }
        mSubWindowHasData = true;
    }
    return ret;
}

int JniVideoRenderer::renderLastFrame() {
    std::lock_guard<std::mutex> lk(mMutex);
    int ret = 0;
    ANativeWindow_Buffer buffer, subBuffer;
    if (mWindow && mWindowBuffer.width > 0 && mWindowBuffer.height) {
        if ((ret = writeBufferToWindow(mWindow, buffer, mWindowBuffer.width,
                                       mWindowBuffer.height)) < 0) {
            return ret;
        }
        copyRGBABuffer(buffer, mWindowBuffer);
    }
    if (mSubWindow && mSubWindowBuffer.width > 0 && mSubWindowBuffer.height > 0) {
        if ((ret = writeBufferToWindow(mSubWindow, subBuffer, mSubWindowBuffer.width,
                                       mSubWindowBuffer.height)) < 0) {
            return ret;
        }
        copyRGBABuffer(subBuffer, mSubWindowBuffer);
        mSubWindowHasData = true;
    }
    return internalRenderFrame();
}

int JniVideoRenderer::renderFrame() {
    std::lock_guard<std::mutex> lk(mMutex);
    return internalRenderFrame();
}

int JniVideoRenderer::writeFrameToWindow(AVFrame *frame, ANativeWindow *window,
                                         ANativeWindow_Buffer& buffer) {
    int ret = writeBufferToWindow(window, buffer, frame->width, frame->height);
    if (ret < 0) {
        return ret;
    }

    int bufferLineSize = buffer.stride * 4;
    av_image_copy((uint8_t**) &buffer.bits, &bufferLineSize, (const uint8_t **) frame->data,
                  frame->linesize, (AVPixelFormat) frame->format, buffer.width, buffer.height);

    return 0;
}

int JniVideoRenderer::writeBufferToWindow(ANativeWindow *window, ANativeWindow_Buffer &buffer,
                                          int width, int height) {
    if (!window) {
        return 0;
    }
    int ret;
    ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888);
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
    return 0;
}

int JniVideoRenderer::internalRenderFrame() {
    int ret = 0;
    if (mWindow && (ret = ANativeWindow_unlockAndPost(mWindow)) < 0) {
        return ret;
    }
    if (mSubWindow && mSubWindowHasData) {
        ret = ANativeWindow_unlockAndPost(mSubWindow);
    }
    mSubWindowHasData = false;
    return ret;
}
