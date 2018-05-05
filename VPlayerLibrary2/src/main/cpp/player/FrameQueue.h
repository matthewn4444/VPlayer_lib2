#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

#define MAX_FRAME_QUEUE_SIZE 16

#include <atomic>
#include <mutex>
#include <vector>
#include <condition_variable>
#include "PacketQueue.h"
#include "Frame.h"

class FrameQueue {          // TODO use deque c++ object
public:
    FrameQueue(int streamIndex, bool isAVQueue, size_t maxSize);
    ~FrameQueue();

    void abort();
    void push();
    void pushNext();

    Frame* peekFirst();
    Frame* peekNext();
    Frame* peekLast();
    Frame* peekWritable();
    Frame* peekReadable();
    int getNumRemaining();
    int64_t getLastPos();

    PacketQueue* getPacketQueue() {
        return &mPktQueue;
    }

    std::mutex& getMutex() {
        return mMutex;
    }

    bool isReadIndexShown() {
        return mReadIndexShown > 0;
    }

    size_t capacity() {
        return mMaxSize;
    }

private:
    std::vector<Frame*> mQueue;
    int mReadIndex;
    int mWriteIndex;
    std::atomic<int> mSize;
    size_t mMaxSize;
    bool mKeepLast;
    char mReadIndexShown;
    std::mutex mMutex;
    std::condition_variable mCondition;
    PacketQueue mPktQueue;
};


#endif //FRAMEQUEUE_H
