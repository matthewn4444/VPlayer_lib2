#include "Frame.h"

static const char* sTag = "Frame";

Frame::Frame(bool useAVFrame) :
        hasUploaded(false),
        flipVertical(false),
        mDuration(0),
        mMaxDuration(0),
        mWidth(0),
        mHeight(0),
        mFormat(0),
        mFilePos(0),
        mSerial(-1),
        mIsAVFrame(useAVFrame) {
    mBuffer = {0};
    mSar = {0};
    if (useAVFrame) {
        mBuffer.frame = av_frame_alloc();
        if (mBuffer.frame == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate frame");
        }
    } else {
        __android_log_print(ANDROID_LOG_INFO, sTag, "Constuctor Frame for subtittles");
        mBuffer.subtitle = NULL;
    }
}

Frame::~Frame() {
    if (mIsAVFrame) {
        av_frame_unref(mBuffer.frame);
        av_frame_free(&mBuffer.frame);
        mBuffer.frame = NULL;
    } else {
        if (mBuffer.subtitle) {
            avsubtitle_free(mBuffer.subtitle);
            mBuffer.subtitle = NULL;
        }
    }
}

bool Frame::reset() {
    if (mIsAVFrame) {
        if (mBuffer.frame == NULL) {
            return false;
        }
        av_frame_unref(mBuffer.frame);
    } else {
        if (mBuffer.subtitle == NULL) {
            return false;
        }
        avsubtitle_free(mBuffer.subtitle);
    }
    return true;
}

void Frame::setAVFrame(AVFrame *frame, AVRational duration, AVRational tb,
                       intptr_t serial) {
    if (!mIsAVFrame) {
        return;
    }
    mSar = frame->sample_aspect_ratio;
    hasUploaded = false;
    mWidth = frame->width;
    mHeight = frame->height;
    mFormat = frame->format;
    mPts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
    mDuration = duration.num && duration.den ? av_q2d(duration) : 0;
    mFilePos = frame->pkt_pos;
    mSerial = serial;
    av_frame_move_ref(mBuffer.frame, frame);
}

AVFrame *Frame::frame() {
    if (mIsAVFrame) {
        return mBuffer.frame;
    } else {
        return NULL;
    }
}

AVSubtitle *Frame::subtitle() {
    if (!mIsAVFrame) {
        return mBuffer.subtitle;
    } else {
        return NULL;
    }
}

float Frame::startSubTimeMs() {
    if (!mIsAVFrame) {
        return (float) mBuffer.subtitle->start_display_time / 1000;
    }
    return 0;
}


float Frame::endSubTimeMs() {
    if (!mIsAVFrame) {
        return (float) mBuffer.subtitle->end_display_time / 1000;
    }
    return 0;
}




