#ifndef SUBTITLESTREAM_H
#define SUBTITLESTREAM_H

#include "StreamComponent.h"

class SubtitleStream : public StreamComponent {
public:
    SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~SubtitleStream();

protected:
    void onReceiveDecodingFrame(void *frame, int *outRetCode) override;
    int onProcessThread() override;
    void onDecodeFrame(void* frame, AVPacket* pkt, int* outRetCode) override;
};

#endif //SUBTITLESTREAM_H
