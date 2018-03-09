#ifndef FRAME_H
#define FRAME_H

extern "C" {
#include <libavformat/avformat.h>
}
#include <android/log.h>

class Frame {
    union Buffer {
        AVFrame* frame;
        AVSubtitle* subtitle;
    };

public:
    Frame(bool useAVFrame);
    ~Frame();

    bool reset();

    // TODO see if we can just pass the frame itself
    void setAVFrame(AVFrame *frame, AVRational duration, AVRational tb, intptr_t serial);

    intptr_t serial() {
        return mSerial;
    }
    int64_t filePosition() {
        return mFilePos;
    }

    AVRational sampleAspectRatio() {
        return mSar;
    }

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

    int format() {
        return mFormat;
    }

    AVFrame* frame();

    AVSubtitle* subtitle();

    float startSubTimeMs();

    float endSubTimeMs();

    bool hasUploaded;
    bool flipVertical;

private:
    // TODO try to simply this
    const bool mIsAVFrame;
    Buffer mBuffer;

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
