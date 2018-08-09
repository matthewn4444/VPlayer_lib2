#ifndef IMAGESUBHANDLER_H
#define IMAGESUBHANDLER_H

#include <vector>
#include "SubtitleStream.h"
#include "FrameQueue.h"

class ImageSubHandler : public SubtitleStream::SubtitleHandlerBase {
public:
    ImageSubHandler(AVCodecID codecID);
    ~ImageSubHandler();

    int open(AVCodecContext *cContext, AVFormatContext *fContext) override;

    bool handleDecodedSubtitle(AVSubtitle *subtitle, intptr_t pktSerial) override;

    AVSubtitle *getSubtitle() override;

    int blendToFrame(double pts, AVFrame *frame, intptr_t pktSerial, bool force = false) override;

    bool areFramesPending() override;

    void invalidateFrame() override;

private:
    struct FrameCache {
        int x;
        int y;
        AVFrame* frame;
        double lastUsed;
    };

    void blendFrames(AVFrame* dstFrame, AVFrame* srcFrame, int srcX, int srcY);
    int prepareSubFrame(AVSubtitleRect* rect, FrameCache& cache, int vWidth, int vHeight);

    FrameQueue* mQueue;
    std::vector<FrameCache> mFrameCache;
    struct SwsContext* mSwsContext;
    int mCodecWidth;
    bool mInvalidate;
};

#endif //IMAGESUBHANDLER_H
