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
        virtual bool handleDecodedSubtitle(AVSubtitle *subtitle, intptr_t pktSerial) = 0;
        virtual int blendToFrame(double pts, AVFrame *vFrame, intptr_t pktSerial) = 0;
        virtual AVSubtitle* getSubtitle() = 0;
        virtual bool areFramesPending() = 0;

        const AVCodecID codec_id;
    };

    SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~SubtitleStream();

    int blendToFrame(AVFrame* vFrame, Clock* vclock);

protected:
    int open() override;

    void onReceiveDecodingFrame(void *frame, int *outRetCode) override;
    int onProcessThread() override;
    void onDecodeFrame(void* frame, AVPacket* pkt, int* outRetCode) override;

    bool areFramesPending() override;

private:
    SubtitleHandlerBase* mHandler;
};

#endif //SUBTITLESTREAM_H
