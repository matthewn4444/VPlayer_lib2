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
    mSubtitle = {0};
    mSar = {0};
    if (useAVFrame) {
        mFrame = av_frame_alloc();
        if (mFrame == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate frame");
        }
    }
}

Frame::~Frame() {
    if (mIsAVFrame) {
        av_frame_unref(mFrame);
        av_frame_free(&mFrame);
    } else {
        avsubtitle_free(&mSubtitle);
    }
    mFrame = NULL;
}

bool Frame::reset() {
    if (mIsAVFrame) {
        if (mFrame == NULL) {
            return false;
        }
        av_frame_unref(mFrame);
    } else {
        avsubtitle_free(&mSubtitle);
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
    mDuration = duration.num && duration.den ? 1 / av_q2d(duration) : 0;
    mFilePos = frame->pkt_pos;
    mSerial = serial;
    av_frame_move_ref(mFrame, frame);
}

void Frame::updateAsSubtitle(int width, int height, intptr_t serial) {
    if (mIsAVFrame) {
        return;
    }
    AVSubtitle* sub = subtitle();
    if (sub->pts != AV_NOPTS_VALUE) {
        mPts = sub->pts / (double) AV_TIME_BASE;
    } else {
        mPts = AV_NOPTS_VALUE;
    }
    mWidth = width;
    mHeight = height;
    mSerial = serial;
    hasUploaded = false;
}

AVFrame *Frame::frame() {
    if (mIsAVFrame) {
        return mFrame;
    } else {
        return NULL;
    }
}

AVSubtitle *Frame::subtitle() {
    if (!mIsAVFrame) {
        return &mSubtitle;
    } else {
        return NULL;
    }
}

double Frame::startPts() {
    if (!mIsAVFrame) {
        return ((double) mSubtitle.start_display_time / AV_TIME_BASE / 1000) + mPts;
    }
    return 0;
}

double Frame::endPts() {
    if (!mIsAVFrame) {
        return ((double) mSubtitle.end_display_time / AV_TIME_BASE / 1000) + mPts;
    }
    return 0;
}
