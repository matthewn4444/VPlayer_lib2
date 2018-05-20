#ifndef SSAHANDLER_H
#define SSAHANDLER_H

#include "SubtitleStream.h"

class SSAHandler : public SubtitleStream::SubtitleHandlerBase {
public:
    SSAHandler(AVCodecID codecID);
    ~SSAHandler();

    int open(AVCodecContext *cContext, AVFormatContext *fContext) override;

    void setRenderSize(int renderingWidth, int renderingHeight) override;

    bool handleDecodedFrame(Frame *frame, FrameQueue *queue, intptr_t mPktSerial) override;

    int blendToFrame(double pts, AVFrame *vFrame, FrameQueue* queue) override;

    void blendSSA(AVFrame *vFrame, const ASS_Image *subImage);

private:
    std::mutex mAssMutex;
    ASS_Library* mAssLibrary;
    ASS_Renderer* mAssRenderer;
    ASS_Track* mAssTrack;
};

#endif //SSAHANDLER_H
