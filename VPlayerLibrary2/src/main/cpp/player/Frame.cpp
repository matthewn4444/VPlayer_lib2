#include "Frame.h"

static const char* sTag = "Frame";

Frame::Frame(int mediatype) :
        flipVertical(false),
        mDuration(0),
        mMaxDuration(0),
        mWidth(0),
        mHeight(0),
        mFormat(0),
        mFilePos(0),
        mSerial(-1),
        mMediaType(mediatype) {
    mSubtitle = {0};
    mSar = {0};
    if (mediatype == AVMEDIA_TYPE_AUDIO) {
        // Only audio needs this, video will pass preprocessed pointer instead
        mFrame = av_frame_alloc();
        if (mFrame == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate frame");
        }
    }
}

Frame::~Frame() {
    if (mMediaType == AVMEDIA_TYPE_AUDIO) {
        av_frame_unref(mFrame);
        av_frame_free(&mFrame);
    } else if (mMediaType == AVMEDIA_TYPE_SUBTITLE) {
        avsubtitle_free(&mSubtitle);
    }
    mFrame = NULL;
}

bool Frame::reset() {
    if (mMediaType == AVMEDIA_TYPE_AUDIO) {
        if (mFrame == NULL) {
            return false;
        }
        av_frame_unref(mFrame);
    } else if (mMediaType == AVMEDIA_TYPE_SUBTITLE) {
        avsubtitle_free(&mSubtitle);
    }
    return true;
}

void Frame::setAVFrame(AVFrame *frame, AVRational duration, AVRational tb,
                       intptr_t serial) {
    if (mMediaType == AVMEDIA_TYPE_SUBTITLE) {
        return;
    }
    mSar = frame->sample_aspect_ratio;
    mWidth = frame->width;
    mHeight = frame->height;
    mFormat = frame->format;
    mPts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
    mDuration = duration.num && duration.den ? 1 / av_q2d(duration) : 0;
    mFilePos = frame->pkt_pos;
    mSerial = serial;
    if (mMediaType == AVMEDIA_TYPE_VIDEO) {
        mFrame = frame;
    } else {
        av_frame_move_ref(mFrame, frame);
    }
}

void Frame::updateAsSubtitle(int width, int height, intptr_t serial) {
    if (mMediaType != AVMEDIA_TYPE_SUBTITLE) {
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
}

AVFrame *Frame::frame() {
    if (mMediaType != AVMEDIA_TYPE_SUBTITLE) {
        return mFrame;
    } else {
        return NULL;
    }
}

AVSubtitle *Frame::subtitle() {
    if (mMediaType == AVMEDIA_TYPE_SUBTITLE) {
        return &mSubtitle;
    } else {
        return NULL;
    }
}

double Frame::startPts() {
    if (mMediaType == AVMEDIA_TYPE_SUBTITLE) {
        return ((double) mSubtitle.start_display_time / AV_TIME_BASE / 1000) + mPts;
    }
    return 0;
}

double Frame::endPts() {
    if (mMediaType == AVMEDIA_TYPE_SUBTITLE) {
        return ((double) mSubtitle.end_display_time / AV_TIME_BASE / 1000) + mPts;
    }
    return 0;
}
