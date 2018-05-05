#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

extern "C" {
#include <libavcodec/avcodec.h>
}
#include <android/log.h>
#include <mutex>
#include <condition_variable>

typedef struct AVPacketNode  {
    AVPacket pkt;
    struct AVPacketNode *next;
    intptr_t serial;
} AVPacketNode;

class PacketQueue {
public:
    PacketQueue(int streamIndex);
    ~PacketQueue();

    int dequeue(AVPacket *pkt, intptr_t *serial, bool block);
    void begin(AVPacket *flushPacket);
    void abort();
    void flush();
    int flushPackets(AVPacket *flushPkt);
    int enqueue(AVPacket* pkt, bool flush = false);
    int enqueueEmpty();

    inline intptr_t& serial() {
        return mSerial;
    }

    inline int size() {
        return mSize;
    }

    inline int numPackets() {
        return mNumPackets;
    }

    inline int64_t duration() {
        return mDuration;
    }

    inline bool hasAborted() {
        return mAbort;
    }

private:
    int enqueueLocked(AVPacket* pkt, bool flush);

    int mStreamIndex;
    AVPacketNode *mFirstPkt, *mLastPkt;
    int mNumPackets;
    int mSize;
    int64_t mDuration;
    bool mAbort;
    intptr_t mSerial;
    std::mutex mMutex;
    std::condition_variable mCondition;
};


#endif //PACKETQUEUE_H
