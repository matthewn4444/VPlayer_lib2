#include "SubtitleStream.h"
#include "SSAHandler.h"
#include "ImageSubHandler.h"

#define SUBPICTURE_QUEUE_SIZE 16

static const char* sTag = "SubtitleStream";

#define _log(...) __android_log_print(ANDROID_LOG_INFO, sTag, __VA_ARGS__);

static bool isTextSub(AVCodecID id) {
    return id == AV_CODEC_ID_ASS || id == AV_CODEC_ID_SRT || id == AV_CODEC_ID_TEXT;
}

SubtitleStream::SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback) :
        StreamComponent(context, AVMEDIA_TYPE_SUBTITLE, flushPkt, callback, SUBPICTURE_QUEUE_SIZE),
        mHandler(NULL) {
}

SubtitleStream::~SubtitleStream() {
    if (mHandler) {
        delete mHandler;
        mHandler = NULL;
    }
}

void SubtitleStream::setRendererSize(int width, int height) {
    mRenderWidth = width;
    mRenderHeight = height;
    if (mHandler) {
        mHandler->setRenderSize(width, height);
    }
}


int SubtitleStream::blendToFrame(AVFrame *vFrame, Clock* vclock) {
    if (mHandler) {
        mHandler->blendToFrame(vclock->getPts(), vFrame, mQueue);
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
            } else {
                mHandler->setRenderSize(mRenderWidth, mRenderHeight);
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

int SubtitleStream::onProcessThread() {
    _log("Process subtitle thread start");
    Frame* sp;
    int ret;
    while (1) {
        if (!(sp = mQueue->peekWritable())) {
            return 0;
        }
        if ((ret = decodeFrame(sp->subtitle())) < 0) {
            break;
        } else if (ret) {
            if (!mHandler->handleDecodedFrame(sp, mQueue, mPktSerial)) {
                __android_log_print(ANDROID_LOG_WARN, sTag, "Does not support type of subtitle %d",
                                    sp->subtitle()->format);
                avsubtitle_free(sp->subtitle());
            }
        }
    }
    return 0;
}

void SubtitleStream::onReceiveDecodingFrame(void *frame, int *outRetCode) {
    // Do nothing
}
