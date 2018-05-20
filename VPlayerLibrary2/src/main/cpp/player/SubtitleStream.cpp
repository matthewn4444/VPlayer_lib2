#include "SubtitleStream.h"
#include "SSAHandler.h"
#include "ImageSubHandler.h"

static const char* sTag = "SubtitleStream";

#define _log(...) __android_log_print(ANDROID_LOG_INFO, sTag, __VA_ARGS__);

static bool isTextSub(AVCodecID id) {
    return id == AV_CODEC_ID_ASS || id == AV_CODEC_ID_SRT || id == AV_CODEC_ID_TEXT;
}

SubtitleStream::SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback) :
        StreamComponent(context, AVMEDIA_TYPE_SUBTITLE, flushPkt, callback),
        mHandler(NULL) {
}

SubtitleStream::~SubtitleStream() {
    if (mHandler) {
        delete mHandler;
        mHandler = NULL;
    }
}

int SubtitleStream::blendToFrame(AVFrame *vFrame, Clock* vclock) {
    if (mHandler) {
        mHandler->blendToFrame(vclock->getPts(), vFrame, mPacketQueue->serial());
    }
    return 0;
}

int SubtitleStream::open() {
    int ret = StreamComponent::open();

    if (!ret) {
        // Only create a handler if not exists or previous handler is still same type
        if (isTextSub(mCContext->codec_id)) {
            if (mHandler == NULL || !isTextSub(mHandler->codec_id)) {
                mHandler = new SSAHandler(mCContext->codec_id);
            }
        } else if (mHandler == NULL || isTextSub(mHandler->codec_id)) {
            mHandler = new ImageSubHandler(mCContext->codec_id);
        }

        if (mHandler) {
            if ((ret = mHandler->open(mCContext, mFContext)) < 0) {
                __android_log_print(ANDROID_LOG_WARN, sTag, "Cannot open subtitles handler, skip");
                delete mHandler;
                mHandler = NULL;
            }
        } else {
            __android_log_print(ANDROID_LOG_WARN, sTag, "No subtitle handler for type %d",
                                mCContext->codec_id);
        }
    }
    return ret;
}

void SubtitleStream::onDecodeFrame(void* frame, AVPacket *pkt, int *ret) {
    int gotFrame = 0;
    if ((*ret = avcodec_decode_subtitle2(mCContext, (AVSubtitle*) frame, &gotFrame, pkt)) < 0) {
        *ret = AVERROR(EAGAIN);
    } else {
        if (gotFrame && !pkt->data) {
            mPktPending = true;
            av_packet_move_ref(&mPkt, pkt);
        }
        *ret = gotFrame ? 0 : (pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
    }
}

bool SubtitleStream::areFramesPending() {
    if (!mHandler) {
        return false;
    }
    return mHandler->areFramesPending();
}

int SubtitleStream::onProcessThread() {
    _log("Process subtitle thread start");
    int ret;
    while (1) {
        AVSubtitle* subtitle = mHandler->getSubtitle();
        if (!subtitle || (ret = decodeFrame(subtitle)) < 0) {
            break;
        } else if (ret && !mHandler->handleDecodedSubtitle(subtitle, mPktSerial)) {
            __android_log_print(ANDROID_LOG_WARN, sTag, "Does not support type of subtitle %d",
                                subtitle->format);
            avsubtitle_free(subtitle);
        }
    }
    return 0;
}

void SubtitleStream::onReceiveDecodingFrame(void *frame, int *outRetCode) {
    // Do nothing
}
