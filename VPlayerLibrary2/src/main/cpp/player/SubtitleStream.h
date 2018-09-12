#ifndef SUBTITLESTREAM_H
#define SUBTITLESTREAM_H

#include <ass/ass.h>
#include "StreamComponent.h"
#include "SubtitleFrameQueue.h"

// TODO make this base class, have SSA and image (pgs) subs into subclasses
class SubtitleStream : public StreamComponent {
public:

    class SubtitleHandlerBase {
    public:
        SubtitleHandlerBase(AVCodecID codecID) : codec_id(codecID) {}
        virtual ~SubtitleHandlerBase() {}
        virtual int open(AVCodecContext* cContext, AVFormatContext* fContext) = 0;
        virtual bool handleDecodedSubtitle(AVSubtitle *subtitle, intptr_t pktSerial) = 0;
        virtual int blendToFrame(double pts, AVFrame *vFrame, intptr_t pktSerial, bool force) = 0;
        virtual void setDefaultFont(const char* fontPath, const char* fontFamily) = 0;
        virtual AVSubtitle* getSubtitle() = 0;
        virtual bool areFramesPending() = 0;
        virtual void invalidateFrame() = 0;
        virtual void flush() = 0;

        const AVCodecID codec_id;
    };

    SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~SubtitleStream();

    // If subtitle stream is handling its own frame, use these functions to prepare and get it
    int prepareSubtitleFrame(int64_t pts, double clockPts, bool force = false);
    AVFrame* getPendingSubtitleFrame(int64_t pts);

    // If you want to merge the subs into an existing frame use this
    int blendToFrame(AVFrame* vFrame, double clockPts, bool force = false);

    void setFrameSize(int width, int height);
    void setDefaultFont(const char* fontPath, const char* fontFamily);

protected:
    int open() override;

    void onReceiveDecodingFrame(void *frame, int *outRetCode) override;
    int onProcessThread() override;
    void onDecodeFrame(void* frame, AVPacket* pkt, int* outRetCode) override;
    void onDecodeFlushBuffers() override;

    bool areFramesPending() override;

private:
    int ensureQueue();

    SubtitleHandlerBase* mHandler;
    SubtitleFrameQueue* mFrameQueue;

    int mPendingWidth;
    int mPendingHeight;

    const char* mPendingFontPath;
    const char* mPendingFontFamily;
};

#endif //SUBTITLESTREAM_H
