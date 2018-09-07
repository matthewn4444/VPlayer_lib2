#ifndef VIDEOSTREAM_H
#define VIDEOSTREAM_H

#define VIDEO_PIC_QUEUE_SIZE 3

#include "AVComponentStream.h"
#include "IVideoRenderer.h"
#include "SubtitleStream.h"
#include "AvFramePool.h"
#include "YUV16to8Converter.h"

class VideoStream : public AVComponentStream {
public:
    VideoStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~VideoStream();

    void setPaused(bool paused) override;

    void setFrameStepMode(bool flag) {
        mFrameStepMode = flag;
    }

    void setVideoRenderer(IVideoRenderer* videoRenderer);

    void setSubtitleComponent(SubtitleStream* stream);

    void setSupportNetworkControls(bool flag);

    bool canEnqueueStreamPacket(const AVPacket& packet) override;
    bool allowFrameDrops();

    float getAspectRatio();

protected:
    int onProcessThread() override;
    int onRenderThread() override;
    void onAVFrameReceived(AVFrame *frame) override;

    int open() override;
    AVDictionary* getPropertiesOfStream(AVCodecContext*, AVStream*, AVCodec*) override;

private:
    int processVideoFrame(AVFrame* avFrame, AVFrame** outFrame);
    int synchronizeVideo(double *remainingTime);
    double getFrameDurationDiff(Frame* frame, Frame* nextFrame);
    void spawnRendererThreadIfHaveNot();
    int writeFrameToRender(AVFrame* frame);

    IVideoRenderer* mVideoRenderer;
    SubtitleStream* mSubStream;
    bool mNextFrameWritten;
    bool mFrameStepMode;
    bool mForceRefresh;
    bool mAllowDropFrames;
    double mFrameTimer;
    int mEarlyFrameDrops;
    int mLateFrameDrops;
    long mMaxFrameDuration;
    bool mCanSupportNetworkControls;
    AvFramePool mFramePool;
    BasicYUVConverter* mCSConverter;
};

#endif //VIDEOSTREAM_H