#include "SubtitleStream.h"
#include "SSAHandler.h"
#include "ImageSubHandler.h"

#define VIDEO_PIC_QUEUE_SIZE 4
#define DEFAULT_FRAME_WIDTH 640
#define DEFAULT_FRAME_HEIGHT 480

static const char* sTag = "SubtitleStream";

#define _log(...) __android_log_print(ANDROID_LOG_INFO, sTag, __VA_ARGS__);

static bool isTextSub(AVCodecID id) {
    return id == AV_CODEC_ID_ASS || id == AV_CODEC_ID_SRT || id == AV_CODEC_ID_TEXT;
}

SubtitleStream::SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback) :
        StreamComponent(context, AVMEDIA_TYPE_SUBTITLE, flushPkt, callback),
        mHandler(NULL),
        mFrameQueue(NULL),
        mPendingWidth(0),
        mPendingHeight(0) {
}

SubtitleStream::~SubtitleStream() {
    if (mHandler) {
        delete mHandler;
        mHandler = NULL;
    }
    if (mFrameQueue) {
        delete mFrameQueue;
        mFrameQueue = NULL;
    }
}

int SubtitleStream::prepareSubtitleFrame(int64_t pts, double clockPts) {
    int ret = ensureQueue();
    if (ret < 0) {
        return ret;
    }
    AVFrame* subTmpFrame = mFrameQueue->getNextFrame();
    subTmpFrame->pts = pts;
    if ((ret = blendToFrame(subTmpFrame, clockPts)) < 0) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Failed to blend subs to sub videoFrame");
    } else if (ret > 0) {
        // Has Changed, add it to the list
        mFrameQueue->pushNextFrame();
    }
    return 0;
}

AVFrame *SubtitleStream::getPendingSubtitleFrame(int64_t pts) {
    int ret = ensureQueue();
    if (ret < 0) {
        return NULL;
    }
    AVFrame *subFrame = NULL;
    while (mFrameQueue->getFirstFrame()) {
        AVFrame *f = mFrameQueue->getFirstFrame();
        if (f == NULL || f->pts < pts) {
            break;
        }
        subFrame = mFrameQueue->dequeue();
    }
    return subFrame;
}

int SubtitleStream::blendToFrame(AVFrame *vFrame, double clockPts, bool force) {
    if (mHandler) {
        return mHandler->blendToFrame(clockPts, vFrame, mPacketQueue->serial(), force);
    }
    return 0;
}

void SubtitleStream::setFrameSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    // Limit the upperbound of the frame size to original subtitle width and height
    if (mCContext->width > 0 && mCContext->height > 0) {
        width = std::min(width, mCContext->width);
        height = std::min(height, mCContext->height);
    }

    mPendingWidth = width;
    mPendingHeight = height;
    if (mFrameQueue == NULL) {
        mFrameQueue = new SubtitleFrameQueue();
    }
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
                return ret;
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

int SubtitleStream::ensureQueue() {
    int ret = 0;
    if (mFrameQueue == NULL) {
        int width = mCContext->width > 0 ? mCContext->width : DEFAULT_FRAME_WIDTH;
        int height = mCContext->height > 0 ? mCContext->height : DEFAULT_FRAME_HEIGHT;
        setFrameSize(width, height);
    }
    if (mPendingWidth != mFrameQueue->getWidth() || mPendingHeight != mFrameQueue->getHeight()) {
        if ((ret = mFrameQueue->resize(VIDEO_PIC_QUEUE_SIZE, mPendingWidth, mPendingHeight,
                                       AV_PIX_FMT_RGBA)) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to create subtitle queue");
        } else if (mHandler != NULL) {
            mHandler->invalidateFrame();
        }
    }
    return ret;
}
