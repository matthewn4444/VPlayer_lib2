#include "AVComponentStream.h"
#define sTag "AVComponentStream"

#define _log(...) __android_log_print(ANDROID_LOG_INFO, "AVStreamComponent", __VA_ARGS__);

AVComponentStream::AVComponentStream(AVFormatContext *context, enum AVMediaType type,
                                     AVPacket* flushPkt, ICallback* callback, size_t maxSize) :
        StreamComponent(context, type, flushPkt, callback),
        mClock(NULL),
        mQueue(NULL),
        mQueueMaxSize(maxSize),
        mRenderThread(NULL) {
}

AVComponentStream::~AVComponentStream() {
    internalCleanUp();
    if (mQueue) {
        delete mQueue;
        mQueue = NULL;
    }
}

void AVComponentStream::setPaused(bool paused) {
    mClock->paused = paused;
    StreamComponent::setPaused(paused);
    if (!paused) {
        mRenderPauseCondition.notify_all();
    }
}

int AVComponentStream::open() {
    internalCleanUp();

    int ret = StreamComponent::open();
    if (ret >= 0) {
        mClock = new Clock(&mPacketQueue->serial());
        mQueue = new FrameQueue(type(), mQueueMaxSize);
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

bool AVComponentStream::areFramesPending() {
    return mQueue->getNumRemaining() > 0;
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
    // Render thread can only spawn if decoding has already started
    if (!hasStartedDecoding()) {
        return;
    }
    if (mRenderThread) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Render thread already running, wait for join.");
        mRenderThread->join();
        __android_log_print(ANDROID_LOG_DEBUG, sTag, "Render thread joined, spawn new one.");
    }
    mRenderThread = new std::thread(&AVComponentStream::internalRenderThread, this);
}

void AVComponentStream::waitIfRenderPaused() {
    std::mutex waitMutex;
    while (isPaused()) {
        std::unique_lock<std::mutex> lk(waitMutex);
        mRenderPauseCondition.wait(lk, [this] {return hasAborted() || !isPaused();});
        if (hasAborted()) {
            break;
        }
    }
}

void AVComponentStream::internalRenderThread() {
    IPlayerCallback::UniqueCallback unCallback(mPlayerCallback);
    onRenderThread();
}

void AVComponentStream::internalCleanUp() {
    abort();
    if (mQueue) {
        mQueue->abort();
    }

    if (mRenderThread) {
        mRenderPauseCondition.notify_all();
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
