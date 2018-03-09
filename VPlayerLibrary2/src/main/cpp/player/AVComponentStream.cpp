#include "AVComponentStream.h"
#define sTag "AVComponentStream"

#define _log(...) __android_log_print(ANDROID_LOG_INFO, "AVStreamComponent", __VA_ARGS__);

AVComponentStream::AVComponentStream(AVFormatContext *context, enum AVMediaType type,
                                     AVPacket* flushPkt, ICallback* callback, size_t maxSize) :
        StreamComponent(context, type, flushPkt, callback, maxSize),
        mClock(NULL),
        mRenderThread(NULL) {
}

AVComponentStream::~AVComponentStream() {
    internalCleanUp();
}

void AVComponentStream::setPaused(bool paused, int pausePlayRet) {
    mClock->paused = paused;

}

int AVComponentStream::open() {
    internalCleanUp();

    int ret = StreamComponent::open();
    if (ret >= 0) {
        mClock = new Clock(&getPacketQueue()->serial());
    }
    return ret;
}

void AVComponentStream::onDecodeFrame(void *frame, AVPacket *pkt, int *outRetCode) {
    if (avcodec_send_packet(mCContext, pkt) == AVERROR(EAGAIN)) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Receive_frame and send_packet both returned"
                " EAGAIN, which is an API violation.");
        mPktPending = true;
        av_packet_move_ref(&mPkt, pkt);
    }
}

void AVComponentStream::onReceiveDecodingFrame(void *frame, int *ret) {
    AVFrame* f = (AVFrame*) frame;
    if ((*ret = avcodec_receive_frame(mCContext, f)) >= 0) {
        onAVFrameReceived(f);
    }
}

int AVComponentStream::onProcessThread() {
    mRenderThread = new std::thread(&AVComponentStream::onRenderThread, this);
    return 0;
}

void AVComponentStream::spawnRenderThread() {
    if (mRenderThread) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Render thread already running, wait for join.");
        mRenderThread->join();
        __android_log_print(ANDROID_LOG_DEBUG, sTag, "Render thread joined, spawn new one.");
    }
    mRenderThread = new std::thread(&AVComponentStream::internalRenderThread, this);
}

void AVComponentStream::internalRenderThread() {
    IPlayerCallback::UniqueCallback unCallback(mPlayerCallback);
    onRenderThread();
}

void AVComponentStream::internalCleanUp() {
    if (mQueue) {
        mQueue->abort();
    }

    if (mRenderThread) {
        __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Join render thread %s", typeName());
        mRenderThread->join();
        mRenderThread = NULL;
        __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Joined rendering finished %s", typeName());
    }

    // Clean up previous runs
    if (mClock) {
        delete mClock;
        mClock = NULL;
    }
}



