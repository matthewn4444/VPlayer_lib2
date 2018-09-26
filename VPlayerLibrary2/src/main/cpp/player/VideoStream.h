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

    void setVideoRenderer(IVideoRenderer* videoRenderer);

    void setVideoStreamCallback(IVideoStreamCallback* callback);

    void setSubtitleComponent(SubtitleStream* stream);

    void setSupportNetworkControls(bool flag);

    void invalidNextFrame();

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
    IVideoStreamCallback* mVideoStreamCallback;
    std::atomic<bool> mNextFrameWritten;
    bool mForceRefresh;
    bool mAllowDropFrames;
    bool mInvalidateSubs;
    double mFrameTimer;
    int mEarlyFrameDrops;
    int mLateFrameDrops;
    long mMaxFrameDuration;
    bool mCanSupportNetworkControls;
    AvFramePool mFramePool;
    BasicYUVConverter* mCSConverter;
};

#endif //VIDEOSTREAM_H