#ifndef AVCOMPONENTSTREAM_H
#define AVCOMPONENTSTREAM_H

#include "StreamComponent.h"
#include "FrameQueue.h"
#include "Clock.h"

static const double AV_COMP_NOSYNC_THRESHOLD = 10.0;

class AVComponentStream : public StreamComponent {
public:
    AVComponentStream(AVFormatContext* context, enum AVMediaType type, AVPacket* flushPkt,
                      ICallback* callback, size_t maxSize);
    virtual ~AVComponentStream();

    void setPaused(bool paused, int pausePlayRet) override;

    inline Clock* getClock() {
        return mClock;
    }

protected:
    virtual int onRenderThread() = 0;
    virtual void onAVFrameReceived(AVFrame *frame) = 0;

    int onProcessThread() override;
    void onReceiveDecodingFrame(void *frame, int *outRetCode) override;
    void onDecodeFrame(void* frame, AVPacket* pkt, int* outRetCode) override;
    bool areFramesPending() override;
    void spawnRenderThread();

    bool hasRendererThreadStarted() {
        return mRenderThread != NULL;
    }

    int open() override;
    void internalCleanUp();

    FrameQueue* mQueue;
    Clock* mClock;

private:
    void internalRenderThread();

    size_t mQueueMaxSize;
    std::thread* mRenderThread;
};

#endif //AVCOMPONENTSTREAM_H
