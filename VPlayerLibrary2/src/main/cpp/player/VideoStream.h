#ifndef VIDEOSTREAM_H
#define VIDEOSTREAM_H

#define VIDEO_PIC_QUEUE_SIZE 3

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "AVComponentStream.h"
#include "IVideoRenderer.h"
#include "SubtitleStream.h"
#include "AvFramePool.h"

class VideoStream : public AVComponentStream {
public:
    VideoStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~VideoStream();

    void setPaused(bool paused, int pausePlayRet) override;

    void setFrameStepMode(bool flag) {
        mFrameStepMode = flag;
    }

    void setVideoRenderer(IVideoRenderer* videoRenderer);

    void setSubtitleComponent(SubtitleStream* stream);

    bool canEnqueueStreamPacket(const AVPacket& packet) override;
    bool allowFrameDrops();

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

    IVideoRenderer* mVideoRenderer;
    SubtitleStream* mSubStream;
    bool mFrameStepMode;
    bool mForceRefresh;
    bool mAllowDropFrames;
    double mFrameTimer;
    int mEarlyFrameDrops;
    int mLateFrameDrops;
    long mMaxFrameDuration;

    AvFramePool mFramePool;
    struct SwsContext* mSwsContext;
};

#endif //VIDEOSTREAM_H