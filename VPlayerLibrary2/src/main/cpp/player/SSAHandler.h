#ifndef SSAHANDLER_H
#define SSAHANDLER_H

#include "ASSRenderer.h"
#include "SubtitleStream.h"

class SSAHandler : public SubtitleStream::SubtitleHandlerBase {
public:
    SSAHandler(AVCodecID codecID);
    ~SSAHandler();

    int open(AVCodecContext *cContext, AVFormatContext *fContext) override;

    void abort() override;

    bool handleDecodedSubtitle(AVSubtitle* subtitle, intptr_t pktSerial) override;

    AVSubtitle *getSubtitle() override;

    int blendToFrame(double pts, AVFrame *frame, intptr_t pktSerial, bool force = false) override;

    void setDefaultFont(const char *fontPath, const char *fontFamily) override;

    bool areFramesPending() override;

    void invalidateFrame() override;

    void flush() override;

private:
    std::mutex mAssMutex;
    ASSRenderer* mRenderer;
    ASS_Track* mAssTrack;
    AVSubtitle mTmpSubtitle;
    int64_t mLastPts;
};

#endif //SSAHANDLER_H
