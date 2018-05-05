#include "FrameQueue.h"
#define sTag "FrameQueue"

FrameQueue::FrameQueue(int streamIndex, bool isAVQueue, size_t maxSize) :
        mReadIndex(0),
        mWriteIndex(0),
        mSize(0),
        mMaxSize(FFMIN(maxSize, MAX_FRAME_QUEUE_SIZE)),
        mReadIndexShown(0),
        mPktQueue(streamIndex),
        mKeepLast(isAVQueue) {
    mQueue.reserve(mMaxSize);
    for (int i = 0; i < mMaxSize; i++) {
        Frame* f = new Frame(isAVQueue);
        mQueue.push_back(f);
    }
}

FrameQueue::~FrameQueue() {
    for (int i = 0; i < mMaxSize; i++) {
        delete mQueue[i];
        mQueue[i] = NULL;
    }
}

void FrameQueue::abort() {
    mPktQueue.abort();
    mCondition.notify_all();
}

void FrameQueue::push() {
    if (++mWriteIndex >= mMaxSize) {
        mWriteIndex = 0;
    }
    mSize++;
    mCondition.notify_one();
}

// TODO try to simplify, always peeks then push
void FrameQueue::pushNext() {
    if (mKeepLast && !mReadIndexShown) {
        mReadIndexShown = 1;
        return;
    }
    mQueue[mReadIndex]->reset();
    if (++mReadIndex >= mMaxSize) {
        mReadIndex = 0;
    }
    mSize--;
    mCondition.notify_one();
}

int FrameQueue::getNumRemaining() {
    return mSize - mReadIndexShown;
}

Frame* FrameQueue::peekFirst() {
    return mQueue[(mReadIndex + mReadIndexShown) % mMaxSize];
}

Frame* FrameQueue::peekNext() {
    return mQueue[(mReadIndex + mReadIndexShown + 1) % mMaxSize];
}

Frame* FrameQueue::peekLast() {
    return mQueue[mReadIndex];
}

Frame* FrameQueue::peekWritable() {
    {
        // Wait for enough space for new frame
        std::unique_lock<std::mutex> lk(mMutex);
        mCondition.wait(lk, [this] {return mSize < mMaxSize || mPktQueue.hasAborted();});
        if (mPktQueue.hasAborted()) {
            return NULL;
        }
    }
    return mQueue[mWriteIndex];
}

Frame* FrameQueue::peekReadable() {
    {
        std::unique_lock<std::mutex> lk(mMutex);
        mCondition.wait(lk, [this] {
            return mSize - mReadIndexShown > 0 || mPktQueue.hasAborted();
        });
        if (mPktQueue.hasAborted()) {
            return NULL;
        }
    }
    return peekFirst();
}

int64_t FrameQueue::getLastPos() {
    Frame* frame = mQueue[mReadIndex];
    if (mReadIndexShown && frame->serial() == mPktQueue.serial()) {
        return frame->filePosition();
    }
    return -1;
}
