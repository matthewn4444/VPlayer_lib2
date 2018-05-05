#ifndef SUBTITLESTREAM_H
#define SUBTITLESTREAM_H

#include <ass/ass.h>
#include "StreamComponent.h"

// TODO make this base class, have SSA and image (pgs) subs into subclasses
class SubtitleStream : public StreamComponent {
public:
    SubtitleStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~SubtitleStream();

    void setRendererSize(int width, int height);
    ASS_Image* getASSImage(double pts);
    bool isSSASubtitle();

    int synchronizeQueue(Clock* vclock);        // TODO Most likely not needed
    int blendToFrame(AVFrame* vFrame);


protected:
    int open() override;

    void onReceiveDecodingFrame(void *frame, int *outRetCode) override;
    int onProcessThread() override;
    void onDecodeFrame(void* frame, AVPacket* pkt, int* outRetCode) override;
    void blendSSA(AVFrame *vFrame, const ASS_Image *subImage);

private:
    std::mutex mASSMutex;
    ASS_Library* mAssLibrary;
    ASS_Renderer* mAssRenderer;
    ASS_Track* mAssTrack;

    int mRenderWidth;
    int mRenderHeight;
    int mAssTrackIndex;
    double mCurrentSubPts;
    double mEndSubPts;
};

#endif //SUBTITLESTREAM_H
