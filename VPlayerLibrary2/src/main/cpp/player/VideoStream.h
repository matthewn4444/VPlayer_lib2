#ifndef VIDEOSTREAM_H
#define VIDEOSTREAM_H

#define VIDEO_PIC_QUEUE_SIZE 3

#include "AVComponentStream.h"
#include "IVideoRenderer.h"
#include "SubtitleStream.h"

class VideoStream : public AVComponentStream {
public:
    VideoStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~VideoStream();

    void setPaused(bool paused, int pausePlayRet) override;

    void setFrameStepMode(bool flag) {
        mFrameStepMode = flag;
    }

    void setVideoRenderer(IVideoRenderer* videoRenderer);

    void setSubtitleComponent(SubtitleStream* stream) {
        mSubStream = stream;
    }

    bool canEnqueueStreamPacket(const AVPacket& packet) override;
    bool allowFrameDrops();

protected:
    int onProcessThread() override;
    int onRenderThread() override;
    void onAVFrameReceived(AVFrame *frame) override;

    int open() override;
    AVDictionary* getPropertiesOfStream(AVCodecContext*, AVStream*, AVCodec*) override;

private:
    int processSubtitleQueue();
    int videoProcess(double *remainingTime);
    double getFrameDurationDiff(Frame* frame, Frame* nextFrame);
    void spawnRendererThreadIfHaveNot();

    IVideoRenderer* mVideoRenderer;
    SubtitleStream* mSubStream;
    bool mFrameStepMode;
    bool mForceRefresh;
    bool mAllowDropFrames;
    double mFrameTimer;
    int mEarlyFrameDrops;
    int mLateFrameDrops;
    long mMaxFrameDuration;
};

#endif //VIDEOSTREAM_H
