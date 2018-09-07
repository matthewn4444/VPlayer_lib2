#ifndef VPLAYER_LIB2_PLAYER_H
#define VPLAYER_LIB2_PLAYER_H

#define MAX_STRING_LENGTH (size_t) 256

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
}
#include <android/log.h>
#include <condition_variable>
#include <thread>
#include <vector>
#include "FrameQueue.h"
#include "VideoStream.h"
#include "AudioStream.h"
#include "SubtitleStream.h"
#include "Clock.h"
#include "IPlayerCallback.h"
#include "IVideoRenderer.h"
#include "IAudioRenderer.h"

#ifndef _log
#define _log(...) __android_log_print(ANDROID_LOG_INFO, "VPlayer2Native", __VA_ARGS__);
#endif

class Player : public StreamComponent::ICallback {
public:
    Player();
    virtual ~Player();

    bool openVideo(const char *stream_file_url);
    void stepNextFrame();
    void setVideoRenderer(IVideoRenderer* videoRenderer);

    void togglePlayback() override;
    IAudioRenderer *createAudioRenderer(AVCodecContext *context) override;
    double getAudioLatency() override;

    Clock* getMasterClock() override;
    Clock* getExternalClock() override;

    void updateExternalClockSpeed() override;

    void onQueueEmpty(StreamComponent* component) override;
    void abort() override;

    bool isAborting() {
        return mAbortRequested;
    }

    bool isPaused() {
        return mIsPaused;
    }

    void setSubtitleFrameSize(int width, int height);
    void setDefaultSubtitleFont(const char* fontPath, const char* fontFamily);

    void setCallback(IPlayerCallback *callback);

    void remeasureAudioLatency();

    void invalidateVideoFrame();
private:
    void reset();
    void sleepMs(long ms);
    int sendMetadataReady(AVFormatContext *context);

    int error(int errorCode, const char *message = NULL);

    void readThread();
    int tOpenStreams(AVFormatContext *context);
    int tReadLoop(AVFormatContext *context);

    AVPacket mFlushPkt;
    const char* mFilepath;      // TODO maybe we won't need this

    std::vector<StreamComponent*> mAVComponents;
    Clock mExtClock;
    VideoStream* mVideoStream;
    AudioStream* mAudioStream;
    SubtitleStream* mSubtitleStream;
    // TODO handle attachments

    // Subtitle variables
    int mSubtitleFrameWidth;
    int mSubtitleFrameHeight;
    char mSubtitleFontPath[MAX_STRING_LENGTH];
    char mSubtitleFontFamily[MAX_STRING_LENGTH];

    IVideoRenderer* mVideoRenderer;
    IPlayerCallback* mCallback;
    std::condition_variable mReadThreadCondition;
    std::thread* mReadThreadId;
    bool mShowVideo;

    bool mAbortRequested;
    bool mSeekRequested;
    bool mAttachmentsRequested;

    // Pause variables
    bool mIsPaused;
    bool mLastPaused;
    std::condition_variable mPauseCondition;

    // Seek variables
    int64_t mSeekPos;
    int64_t mSeekRel;
    bool mIsEOF;        // TODO can we not use this

    // Realtime variables
    bool mInfiniteBuffer;       // TODO see if we can just use isrealtime instead of this

    std::mutex mErrorMutex;
};


#endif //VPLAYER_LIB2_PLAYER_H
