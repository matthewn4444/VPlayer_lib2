#ifndef IMAGESUBHANDLER_H
#define IMAGESUBHANDLER_H

#include <vector>
#include "SubtitleStream.h"

class ImageSubHandler : public SubtitleStream::SubtitleHandlerBase {
public:
    ImageSubHandler(AVCodecID codecID);
    ~ImageSubHandler();

    int open(AVCodecContext *cContext, AVFormatContext *fContext) override;

    void setRenderSize(int renderingWidth, int renderingHeight) override;

    bool handleDecodedFrame(Frame *frame, FrameQueue *queue, intptr_t mPktSerial) override;

    int blendToFrame(double pts, AVFrame *vFrame, FrameQueue* queue) override;

private:
    struct FrameCache {
        int x;
        int y;
        AVFrame* frame;
        double lastUsed;
    };

    void blendFrames(AVFrame* dstFrame, AVFrame* srcFrame, int srcX, int srcY);
    int prepareSubFrame(AVSubtitleRect* rect, FrameCache& cache, int vWidth, int vHeight);

    std::vector<FrameCache> mFrameCache;
    struct SwsContext* mSwsContext;
    int mCodecWidth;
    bool mInvalidate;
};

#endif //IMAGESUBHANDLER_H
