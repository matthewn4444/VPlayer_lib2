#ifndef AUDIOSTREAM_H
#define AUDIOSTREAM_H

extern "C" {
#include <libswresample/swresample.h>
}
#include "AVComponentStream.h"
#include "IAudioRenderer.h"

class AudioStream : public AVComponentStream {
public:
    AudioStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback);
    virtual ~AudioStream();

    void setPaused(bool paused) override;

    void setVolume(float gain);
    void invalidateLatency();
    double getLatency();

protected:
    int onRenderThread() override;

    int onProcessThread() override;
    void onAVFrameReceived(AVFrame *frame) override;
    void onDecodeFlushBuffers() override;
    AVDictionary* getPropertiesOfStream(AVCodecContext*, AVStream*, AVCodec*) override;

    int64_t mStartPts;
    AVRational mStartPtsTb;
    int64_t mNextPts;
    AVRational mNextPtsTb;
private:
    int decodeAudioFrame(AVFrame* frame, int wantedNumSamples, uint8_t **out);
    int syncClocks(AVFrame* frame);

    IAudioRenderer* mAudioRenderer;
    bool mIsMuted;
    bool mLatencyInvalidated;
    bool mPlaybackStateChanged;

    struct SwrContext* mSwrContext;
    uint8_t* mAudioBuffer;
    unsigned int mBufferSize;

    // Computation variables
    double mDiffComputation;
    double mDiffAvgCoef;
    int mDiffAvgCount;
};


#endif //VPLAYER_LIB2_AUDIOSTREAM_H
