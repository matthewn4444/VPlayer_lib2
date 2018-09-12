#ifndef STREAMCOMPONENT_H
#define STREAMCOMPONENT_H

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavformat/avformat.h>
}
#include <thread>
#include <future>
#include <vector>
#include "PacketQueue.h"
#include "IPlayerCallback.h"
#include "IAudioRenderer.h"
#include "Clock.h"

class StreamComponent {
public:
    class ICallback {
    public:
        virtual void abort() = 0;
        virtual IAudioRenderer* createAudioRenderer(AVCodecContext *context) = 0;
        virtual double getAudioLatency() = 0;
        virtual Clock* getMasterClock() = 0;
        virtual Clock* getExternalClock() = 0;
        virtual void updateExternalClockSpeed() = 0;
        virtual void togglePlayback() = 0;
        virtual void onQueueEmpty(StreamComponent* component) = 0;
    };

    StreamComponent(AVFormatContext* context, enum AVMediaType type, AVPacket* flushPkt,
                    ICallback* callback);
    virtual ~StreamComponent();

    virtual void abort();
    int pickBest(int relativeStream = -1);
    int pickByIndex(int streamNumber, bool fromAllStreams = false);
    virtual void setPaused(bool paused);
    void startDecoding();
    void setCallback(IPlayerCallback* callback);

    AVDictionary** getProperties(int* ret);
    bool isQueueFull();
    bool isFinished();
    bool isRealTime();

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

    PacketQueue* getPacketQueue() {
        return mPacketQueue;
    }

protected:
    virtual int decodeFrame(void* frame);

    virtual int onProcessThread() = 0;
    virtual void onDecodeFrame(void* frame, AVPacket* pkt, int* outRetCode) = 0;
    virtual void onReceiveDecodingFrame(void *frame, int *outRetCode) = 0;
    virtual bool areFramesPending() = 0;
    virtual void onDecodeFlushBuffers();

    int error(int code, const char* message = NULL);
    virtual int open();

    virtual void close();
    Clock* getMasterClock();
    Clock* getExternalClock();
    bool isPaused();
    bool hasAborted();
    bool hasStartedDecoding();

    void waitIfPaused();

    std::vector<int> mAvailStreamIndexes;

    virtual AVDictionary* getPropertiesOfStream(AVCodecContext*, AVStream*, AVCodec*);
    AVFormatContext* mFContext;
    AVCodecContext* mCContext;
    PacketQueue* mPacketQueue;
    AVMediaType mType;
    int mStreamIndex;
    IPlayerCallback* mPlayerCallback;
    ICallback* mCallback;
    std::condition_variable mPauseCondition;

    // Decoding variables
    const AVPacket* mFlushPkt;
    AVPacket mPkt;
    intptr_t mPktSerial;
    intptr_t mFinished;
    bool mRequestEnd;           // TODO So far this is never used
    bool mPktPending;
    std::thread* mDecodingThread;

private:
    void internalProcessThread();
    int getCodecInfo(int streamIndex, AVCodecContext** oCContext, AVCodec** oCodec);

    std::mutex mErrorMutex;
    bool mIsRealTime;
};

#endif //STREAMCOMPONENT_H
