#ifndef __SUBTITLEFRAMEQUEUE_H__
#define __SUBTITLEFRAMEQUEUE_H__

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

#include <vector>
#include <atomic>
#include <android/log.h>

class SubtitleFrameQueue {
public:
    SubtitleFrameQueue();
    ~SubtitleFrameQueue();

    int resize(size_t size, int width, int height, AVPixelFormat format);

    AVFrame* getNextFrame();

    AVFrame* getFirstFrame();

    int pushNextFrame();

    AVFrame* dequeue();

    bool isEmpty();

    bool isFull();

    int getWidth();

    int getHeight();

private:
    void reset();
    int nextIndex(int index);

    AVFrame** mFrames;
    bool* mFrameInvalidated;
    size_t mCapacity;
    std::atomic<int> mFrameNextIndex;
    std::atomic<int> mFrameHeadIndex;

    int mWidth;
    int mHeight;
    enum AVPixelFormat mFormat;
};


#endif //__SUBTITLEFRAMEQUEUE_H__
