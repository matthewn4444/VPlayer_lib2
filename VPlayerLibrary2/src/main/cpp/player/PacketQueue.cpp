#include "PacketQueue.h"

PacketQueue::PacketQueue(int streamIndex) :
        mStreamIndex(streamIndex),
        mAbort(true),
        mFirstPkt(NULL),
        mLastPkt(NULL),
        mNumPackets(0),
        mSize(0),
        mDuration(0),
        mSerial(0)
{
}

PacketQueue::~PacketQueue() {
    flush();
}

int PacketQueue::dequeue(AVPacket *pkt, intptr_t *serial, bool block) {
    AVPacketNode* pkt1;
    std::unique_lock<std::mutex> ulk(mMutex);

    while(1) {
        if (mAbort) {
            return AVERROR_EXIT;
        }

        pkt1 = mFirstPkt;
        if (pkt1) {
            mFirstPkt = pkt1->next;
            if (!mFirstPkt) {
                mLastPkt = NULL;
            }
            mNumPackets--;
            mSize -= pkt1->pkt.size + sizeof(*pkt1);
            mDuration -= pkt1->pkt.duration;
            *pkt = pkt1->pkt;
            if (serial) {
                *serial = pkt1->serial;
            }
            av_free(pkt1);
            return 1;
        } else if (!block) {
            return 0;
        } else {
            mCondition.wait(ulk);
        }
    }
}

void PacketQueue::begin(const AVPacket* flushPacket) {
    std::lock_guard<std::mutex> lk(mMutex);
    mAbort = false;
    mSerial = 0;
    enqueueLocked(flushPacket, true);
}

void PacketQueue::abort() {
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mAbort = true;
    }
    mCondition.notify_one();
}

void PacketQueue::flush() {
    std::lock_guard<std::mutex> lk(mMutex);
    AVPacketNode* pkt, *pkt1;
    for (pkt = mFirstPkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    mLastPkt = NULL;
    mFirstPkt = NULL;
    mNumPackets = 0;
    mSize = 0;
    mDuration = 0;
}

int PacketQueue::flushPackets(AVPacket *flushPkt) {
    flush();
    return enqueue(flushPkt, true);
}

int PacketQueue::enqueue(AVPacket* pkt, bool flush) {
    int ret;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        ret = enqueueLocked(pkt, flush);
    }
    if (!flush && ret < 0) {
        av_packet_unref(pkt);
    }
    return ret;
}

int PacketQueue::enqueueEmpty() {
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = mStreamIndex;
    return enqueue(pkt);
}

int PacketQueue::enqueueLocked(const AVPacket* pkt, bool flush) {
    AVPacketNode *pkt1;
    if (mAbort) {
        return AVERROR_EXIT;
    }
    pkt1 = (AVPacketNode*) av_malloc(sizeof(*pkt1));
    if (!pkt1) {
        return AVERROR(ENOMEM);
    }
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (flush) {
        mSerial++;
    }
    pkt1->serial = mSerial;

    if (!mLastPkt) {
        mFirstPkt = pkt1;
    } else {
        mLastPkt->next = pkt1;
    }
    mLastPkt = pkt1;
    mNumPackets++;
    mSize += pkt1->pkt.size + sizeof(*pkt1);
    mDuration+= pkt1->pkt.duration;
    mCondition.notify_one();
    return 0;
}
