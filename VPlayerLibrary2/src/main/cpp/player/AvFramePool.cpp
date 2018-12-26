#include "AvFramePool.h"

static const char* sTag = "AvFramePool";

AvFramePool::AvFramePool() :
        mFrameNextIndex(0),
        mFrameEmptyIndex(0) {
}

AvFramePool::~AvFramePool() {
    reset();
}

int AvFramePool::resize(size_t size, int width, int height, AVPixelFormat format) {
    reset();
    int ret = -1;
    if (size > 0) {
        for (int i = 0; i < size + 1; i++) {
            AVFrame* frame = av_frame_alloc();
            if (!frame) {
                __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to allocate frame in pool");
                return AVERROR(ENOMEM);
            }
            if ((ret = av_image_alloc(frame->data, frame->linesize,width, height, format, 16)) < 0) {
                __android_log_print(ANDROID_LOG_ERROR, sTag,
                                    "Unable to allocate frame data in pool");
                av_frame_free(&frame);
                return ret;
            }
            mFrames.push_back(frame);
        }
        mFrameNextIndex = 0;
        mFrameEmptyIndex = 0;
    }
    mWidth = width;
    mHeight = height;
    mFormat = format;
    return ret;
}

AVFrame *AvFramePool::acquire() {
    AVFrame* frame = mFrames[mFrameNextIndex];
    frame->width = mWidth;
    frame->height = mHeight;
    frame->format = mFormat;
    if (++mFrameNextIndex >= mFrames.size()) {
        mFrameNextIndex = 0;
    }
    return frame;
}

void AvFramePool::reset() {
    for (int i = 0; i < mFrames.size(); i++) {
        av_frame_unref(mFrames[i]);
        av_freep(mFrames[i]);
    }
    mFrames.clear();
}
