#ifndef FRAME_H
#define FRAME_H

extern "C" {
#include <libavformat/avformat.h>
}
#include <android/log.h>

class Frame {
public:
    Frame(int mediatype);
    ~Frame();

    bool reset();

    // TODO see if we can just pass the frame itself
    void setAVFrame(AVFrame *frame, AVRational duration, AVRational tb, intptr_t serial);
    void updateAsSubtitle(int width, int height, intptr_t serial);

    intptr_t serial() {
        return mSerial;
    }
    int64_t filePosition() {
        return mFilePos;
    }

    AVRational sampleAspectRatio() {
        return mSar;
    }

    /**
     * @return Gets the current time of this frame in sec
     */
    double pts() {
        return mPts;
    }

    double duration() {
        return mDuration;
    }

    int width() {
        return mWidth;
    }

    int height() {
        return mHeight;
    }

    AVFrame* frame();

    int format() {
        return mFormat;
    }

    AVSubtitle* subtitle();

    double startPts();

    double endPts();

    bool flipVertical;

private:
    // TODO try to simply this
    AVFrame* mFrame;
    AVSubtitle mSubtitle;
    const int mMediaType;

    // AVFrame usage
    AVRational mSar;
    double mPts;            // Presentation timestamp
    double mDuration;
    long mMaxDuration;
    int mWidth;
    int mHeight;
    int mFormat;

    intptr_t mSerial;
    int64_t mFilePos;
};


#endif //FRAME_H
