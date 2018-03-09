#ifndef AUDIOTRACK_H
#define AUDIOTRACK_H

#include <android/log.h>
#include <mutex>
#include "JniCallbackHandler.h"

static const char* sAudioTrackClassName = "android/media/AudioTrack";
static JavaMethod sMethodAudioTrackWriteSpec = {"write", "([BII)I"};
static JavaMethod sMethodAudioTrackPauseSpec = {"pause", "()V"};
static JavaMethod sMethodAudioTrackPlaySpec = {"play", "()V"};
static JavaMethod sMethodAudioTrackFlushSpec = {"flush", "()V"};
static JavaMethod sMethodAudioTrackStopSpec = {"stop", "()V"};
static JavaMethod sMethodAudioTrackReleaseSpec = {"release", "()V"};
static JavaMethod sMethodAudioTrackChannelCountSpec = {"getChannelCount", "()I"};
static JavaMethod sMethodAudioTrackGetSampleRateSpec = {"getSampleRate", "()I"};
static jmethodID sMethodAudioTrackWrite;
static jmethodID sMethodAudioTrackPause;
static jmethodID sMethodAudioTrackPlay;
static jmethodID sMethodAudioTrackFlush;
static jmethodID sMethodAudioTrackChannelCount;
static jmethodID sMethodAudioTrackGetSampleRate;
static jmethodID sMethodAudioTrackStop;
static jmethodID sMethodAudioTrackRelease;

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

    jobject instance;
private:
    std::mutex mMutex;
    JniCallbackHandler* mJniHandler;

    // Specs
    jint mChannels;
    jint mSampleRate;
    enum AVSampleFormat mFormat;
    int64_t mLayout;
};

#endif //AUDIOTRACK_H
