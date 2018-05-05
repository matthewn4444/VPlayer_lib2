#ifndef AVFRAMEPOOL_H
#define AVFRAMEPOOL_H

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

#include <vector>
#include <android/log.h>

class AvFramePool {
public:
    AvFramePool();
    ~AvFramePool();

    int resize(size_t size, int width, int height, enum AVPixelFormat format);

    AVFrame* acquire();
    void recycle(AVFrame* frame);

private:
    void reset();

    std::vector<AVFrame*> mFrames;
    int mFrameNextIndex;
    int mFrameEmptyIndex;

    int mWidth;
    int mHeight;
    enum AVPixelFormat mFormat;
};

#endif //AVFRAMEPOOL_H
