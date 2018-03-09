#include "SubtitleStream.h"

#define SUBPICTURE_QUEUE_SIZE 16

SubtitleStream::SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback) :
        StreamComponent(context, AVMEDIA_TYPE_SUBTITLE, flushPkt, callback, SUBPICTURE_QUEUE_SIZE) {
}

SubtitleStream::~SubtitleStream() {
}

void SubtitleStream::onDecodeFrame(void* frame, AVPacket *pkt, int *ret) {
    int gotFrame = false;
    if ((*ret = avcodec_decode_subtitle2(mCContext, (AVSubtitle*) frame, &gotFrame, pkt)) < 0) {
        *ret = AVERROR(EAGAIN);
    } else {
        if (gotFrame && !mPkt.data) {
            mPktPending = true;
            av_packet_move_ref(&mPkt, pkt);
        }
        *ret = gotFrame ? 0 : (pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
    }
}

int SubtitleStream::onProcessThread() {
    return 0;
}

void SubtitleStream::onReceiveDecodingFrame(void *frame, int *outRetCode) {
}
