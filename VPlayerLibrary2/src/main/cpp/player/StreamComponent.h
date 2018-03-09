#ifndef STREAMCOMPONENT_H
#define STREAMCOMPONENT_H

extern "C" {
#include <libavutil/pixdesc.h>
}
#include <thread>
#include <future>
#include <vector>
#include "FrameQueue.h"
#include "IPlayerCallback.h"
#include "IAudioRenderer.h"
#include "Clock.h"

class StreamComponent {
public:
    class ICallback {
    public:
        virtual void abort() = 0;
        virtual IAudioRenderer* getAudioRenderer(AVCodecContext* context) = 0;
        virtual Clock* getMasterClock() = 0;
        virtual Clock* getExternalClock() = 0;
        virtual void togglePlayback() = 0;
        virtual void onQueueEmpty(StreamComponent* component) = 0;
    };

    StreamComponent(AVFormatContext* context, enum AVMediaType type, AVPacket* flushPkt,
                    ICallback* callback, size_t queueSize);
    virtual ~StreamComponent();

    void abort();
    int pickBest(int relativeStream = -1);
    int pickByIndex(int streamNumber, bool fromAllStreams = false);
    virtual void setPaused(bool paused, int pausePlayRet);     // TODO, i believe pauseplayret is only for realtime streams, check this (like rtmp)
    void setCallback(IPlayerCallback* callback);

    AVDictionary** getProperties(int* ret);
    bool isQueueFull();
    bool isFinished();

    virtual bool canEnqueueStreamPacket(const AVPacket& packet) {
        return packet.stream_index == getStreamIndex();
    }

    inline int getStreamIndex() {
        return mStreamIndex;
    }

    inline AVStream* getStream() {
        if (mStreamIndex < 0 || mFContext->nb_streams <= mStreamIndex) {
            return NULL;
        }
        return mFContext->streams[mStreamIndex];
    }

    inline const char* typeName() {
        return av_get_media_type_string(mType);
    }

    inline enum AVMediaType type() {
        return mType;
    }

    inline size_t getNumOfStreams() {
        return mAvailStreamIndexes.size();
    }

    inline FrameQueue* getQueue() {
        return mQueue;
    }

    inline PacketQueue* getPacketQueue() {
        if (!mQueue) {
            return NULL;
        }
        return mQueue->getPacketQueue();
    }

protected:
    virtual int decodeFrame(void* frame);

    virtual int onProcessThread() = 0;
    virtual void onDecodeFrame(void* frame, AVPacket* pkt, int* outRetCode) = 0;
    virtual void onReceiveDecodingFrame(void *frame, int *outRetCode) = 0;
    virtual void onDecodeFlushBuffers();

    int error(int code, const char* message = NULL);
    virtual int open();

    virtual void close();
    Clock* getMasterClock();
    Clock* getExternalClock();
    bool isPaused();
    bool hasAborted();

    std::vector<int> mAvailStreamIndexes;

    virtual AVDictionary* getPropertiesOfStream(AVCodecContext*, AVStream*, AVCodec*);
    AVFormatContext* mFContext;
    AVCodecContext* mCContext;
    FrameQueue* mQueue;
    AVMediaType mType;
    int mStreamIndex;
    IPlayerCallback* mPlayerCallback;
    ICallback* mCallback;

    // Decoding variables
    AVPacket mPkt;
    intptr_t mPktSerial;
    intptr_t mFinished;
    bool mRequestEnd;           // TODO So far this is never used
    bool mPktPending;
    std::thread* mDecodingThread;

private:
    int internalOpen();
    void internalProcessThread();
    int getCodecInfo(int streamIndex, AVCodecContext** oCContext, AVCodec** oCodec);
    AVPacket* mFlushPkt;

    // FrameQueue Init conditions
    size_t mQueueMaxSize;

    std::mutex mErrorMutex;
};


#endif //STREAMCOMPONENT_H
