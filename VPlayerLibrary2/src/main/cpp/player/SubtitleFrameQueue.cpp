#include "SubtitleFrameQueue.h"

static const char* sTag = "SubtitleFrameQueue";

SubtitleFrameQueue::SubtitleFrameQueue() :
        mFrames(NULL),
        mFrameInvalidated(NULL),
        mCapacity(0) {
    reset();
}

SubtitleFrameQueue::~SubtitleFrameQueue() {
    reset();
}

int SubtitleFrameQueue::resize(size_t size, int width, int height, AVPixelFormat format) {
    reset();
    mCapacity = size;
    mWidth = width;
    mHeight = height;
    mFormat = format;
    mFrames = new AVFrame*[size];
    mFrameInvalidated = new bool[size];

    int ret;
    if (size > 0) {
        for (int i = 0; i < size; i++) {
            AVFrame* frame = av_frame_alloc();
            if (!frame) {
                __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to allocate sub frame");
                return AVERROR(ENOMEM);
            }
            frame->width = mWidth;
            frame->height = mHeight;
            frame->format = mFormat;
            if ((ret = av_image_alloc(frame->data, frame->linesize,width, height, format, 1)) < 0) {
                __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to image allocate sub frame");
                av_frame_free(&frame);
                return ret;
            }
            mFrames[i] = frame;
            mFrameInvalidated[i] = false;
        }
    }
    return 0;
}

AVFrame *SubtitleFrameQueue::getNextFrame() {
    if (isFull()) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Cannot pull next frame, it is full");
        return NULL;
    }

    // If the next frame is invalidated, then clear its data to be returned
    AVFrame* frame = mFrames[mFrameNextIndex];
    if (mFrameInvalidated[mFrameNextIndex]) {
        mFrameInvalidated[mFrameNextIndex] = false;
        memset(frame->data[0], 0, static_cast<size_t>(frame->height * frame->linesize[0]));
        frame->pts = AV_NOPTS_VALUE;
    }
    return frame;
}

AVFrame* SubtitleFrameQueue::getFirstFrame() {
    return isEmpty() ? NULL : mFrames[mFrameHeadIndex];
}

int SubtitleFrameQueue::pushNextFrame() {
    if (isFull()) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Pulled too many frames out of capacity");
        return -1;
    }
    mFrameNextIndex = nextIndex(mFrameNextIndex);
    return 0;
}

AVFrame* SubtitleFrameQueue::dequeue() {
    if (isEmpty()) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Cannot dequeue when no data in queue!");
        return NULL;
    }

    // Return the removed frame and invalidate its slot, it will be emptied later
    AVFrame* frame = mFrames[mFrameHeadIndex];
    mFrameInvalidated[mFrameHeadIndex] = true;
    mFrameHeadIndex = nextIndex(mFrameHeadIndex);
    return frame;
}

bool SubtitleFrameQueue::isEmpty() {
    return mFrameHeadIndex == mFrameNextIndex;
}

bool SubtitleFrameQueue::isFull() {
    int index = nextIndex(mFrameNextIndex);
    return index == mFrameHeadIndex;
}

int SubtitleFrameQueue::getWidth() {
    return mWidth;
}

int SubtitleFrameQueue::getHeight() {
    return mHeight;
}

void SubtitleFrameQueue::reset() {
    if (mFrames != NULL) {
        for (int i = 0; i < mCapacity; i++) {
            av_frame_unref(mFrames[i]);
            av_freep(mFrames[i]);
        }
        delete[] mFrames;
    }
    if (mFrameInvalidated != NULL) {
        delete[] mFrameInvalidated;
    }
    mFrames = NULL;
    mFrameInvalidated = NULL;
    mCapacity = 0;
    mFrameNextIndex = 0;
    mFrameHeadIndex = 0;
    mWidth = 0;
    mHeight = 0;
    mFormat = AV_PIX_FMT_NONE;
}

int SubtitleFrameQueue::nextIndex(int index) {
    return index + 1 >= mCapacity ? 0 : index + 1;
}
