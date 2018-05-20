#ifndef SUBTITLESTREAM_H
#define SUBTITLESTREAM_H

#include <ass/ass.h>
#include "StreamComponent.h"

// TODO make this base class, have SSA and image (pgs) subs into subclasses
class SubtitleStream : public StreamComponent {
public:

    class SubtitleHandlerBase {
    public:
        SubtitleHandlerBase(AVCodecID codecID) : codec_id(codecID) {}
        virtual ~SubtitleHandlerBase() {}
        virtual int open(AVCodecContext* cContext, AVFormatContext* fContext) = 0;
        virtual void setRenderSize(int renderingWidth, int renderingHeight) = 0;
        virtual bool handleDecodedFrame(Frame *frame, FrameQueue *queue, intptr_t mPktSerial) = 0;
        virtual int blendToFrame(double pts, AVFrame *vFrame, FrameQueue* queue) = 0;

        const AVCodecID codec_id;
    };

    SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~SubtitleStream();

    void setRendererSize(int width, int height);
    int blendToFrame(AVFrame* vFrame, Clock* vclock);

protected:
    int open() override;

    void onReceiveDecodingFrame(void *frame, int *outRetCode) override;
    int onProcessThread() override;
    void onDecodeFrame(void* frame, AVPacket* pkt, int* outRetCode) override;

private:
    SubtitleHandlerBase* mHandler;
    int mRenderWidth;
    int mRenderHeight;
};

#endif //SUBTITLESTREAM_H
