#ifndef AUDIOTRACK_H
#define AUDIOTRACK_H

#include <android/log.h>
#include <mutex>
#include "JniCallbackHandler.h"

// AudioTrack
static const char* sAudioTrackClassName = "android/media/AudioTrack";
static JavaMethod sMethodAudioTrackWriteSpec = {"write", "([BII)I"};
static JavaMethod sMethodAudioTrackPauseSpec = {"pause", "()V"};
static JavaMethod sMethodAudioTrackPlaySpec = {"play", "()V"};
static JavaMethod sMethodAudioTrackFlushSpec = {"flush", "()V"};
static JavaMethod sMethodAudioTrackStopSpec = {"stop", "()V"};
static JavaMethod sMethodAudioTrackSetVolumeSpec = {"setVolume", "(F)I"};
static JavaMethod sMethodAudioTrackReleaseSpec = {"release", "()V"};
static JavaMethod sMethodAudioTrackChannelCountSpec = {"getChannelCount", "()I"};
static JavaMethod sMethodAudioTrackGetSampleRateSpec = {"getSampleRate", "()I"};
static JavaMethod sMethodAudioTrackGetLatencySpec = {"getLatency", "()I"};
static JavaMethod sMethodAudioTrackGetTimestampSpec = {"getTimestamp",
                                                       "(Landroid/media/AudioTimestamp;)Z"};
static jmethodID sMethodAudioTrackWrite;
static jmethodID sMethodAudioTrackPause;
static jmethodID sMethodAudioTrackPlay;
static jmethodID sMethodAudioTrackFlush;
static jmethodID sMethodAudioTrackChannelCount;
static jmethodID sMethodAudioTrackGetSampleRate;
static jmethodID sMethodAudioTrackGetLatency;
static jmethodID sMethodAudioTrackGetTimestamp;
static jmethodID sMethodAudioTrackStop;
static jmethodID sMethodAudioTrackSetVolume;
static jmethodID sMethodAudioTrackRelease;

// AudioTimestamp
static const char* sAudioTimestampClassName = "android/media/AudioTimestamp";
static JavaMethod sMethodAudioTimestampCtorSpec = {"<init>", "()V"};
static JavaField sFieldAudioTimestampFrmPositionSpec = {"framePosition", "J"};
static JavaField sFieldAudioTimestampNanoTimeSpec = {"nanoTime", "J"};
static jmethodID sMethodAudioTimeStampCtor;
static jfieldID sFieldAudioTimestampFramePosition;
static jfieldID sFieldAudioTimestampNanoTime;

class JniCallbackHandler;

class AudioRenderer : public IAudioRenderer {
public:
    static void initJni(JNIEnv *env);

    AudioRenderer(JniCallbackHandler* handler, jobject jAudioTrack, JNIEnv* env);
    ~AudioRenderer();

    int write(uint8_t *data, int len) override;
    int pause() override;
    int play() override;
    int flush() override;
    int stop() override;
    int setVolume(float gain) override;

    int release(JNIEnv* env);

    int numChannels() override {
        return mChannels;
    }

    int sampleRate() override {
        return mSampleRate;
    }

    int64_t layout() override {
        return mLayout;
    }

    enum AVSampleFormat format() override {
        return mFormat;
    }

    double getLatency() override;
    double updateLatency(bool force = false) override;

    jobject instance;
private:
    bool getTimeStamp(long* outFramePosition, long* outNanoTime);
    double getLatencyOldMethod();

    std::mutex mMutex;
    JniCallbackHandler* mJniHandler;

    // Specs
    jint mChannels;
    jint mSampleRate;
    enum AVSampleFormat mFormat;
    int64_t mLayout;

    // Calculate latency
    long mFramesWritten;
    bool mUseTimestampApi;
    long mLastFramePosition;
    long mLastFrameTime;
    double mLastLatencySec;
    int64_t mLastTimestampCheck;
    bool mTimestampStabilizing;
    short mStabilizingCounter;

    // Old latency method
    double* mOldHeadTimeSmoothArr;
    int mOldHeadTimeSmoothArrSize;
    int mOldHeadTimeSmoothArrIndex;
};

#endif //AUDIOTRACK_H
