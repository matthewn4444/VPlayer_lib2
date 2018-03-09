#include "AudioRenderer.h"

const static char *sTag = "Native-AudioTrack";

const static int sAndroidChannelLayout[] = {
        // Mono
        AV_CH_LAYOUT_MONO,
        // Stereo
        AV_CH_LAYOUT_STEREO,
        // 2.1
        AV_CH_LAYOUT_2POINT1,
        // 4.0
        AV_CH_LAYOUT_4POINT0,
        // 4.1
        AV_CH_LAYOUT_4POINT1,
        // 5.1
        AV_CH_LAYOUT_5POINT1_BACK,
        // 6.0
        AV_CH_LAYOUT_6POINT0,
        // 7.0 (front)
        AV_CH_LAYOUT_7POINT0_FRONT,
        // 7.1
        AV_CH_LAYOUT_7POINT1};

void AudioRenderer::initJni(JNIEnv *env) {
    // AudioTrack class
    const jclass clazz = env->FindClass(sAudioTrackClassName);
    sMethodAudioTrackWrite = getJavaMethod(env, clazz, sMethodAudioTrackWriteSpec);
    sMethodAudioTrackPause = getJavaMethod(env, clazz, sMethodAudioTrackPauseSpec);
    sMethodAudioTrackPlay = getJavaMethod(env, clazz, sMethodAudioTrackPlaySpec);
    sMethodAudioTrackFlush = getJavaMethod(env, clazz, sMethodAudioTrackFlushSpec);
    sMethodAudioTrackChannelCount = getJavaMethod(env, clazz, sMethodAudioTrackChannelCountSpec);
    sMethodAudioTrackGetSampleRate = getJavaMethod(env, clazz, sMethodAudioTrackGetSampleRateSpec);
    sMethodAudioTrackStop = getJavaMethod(env, clazz, sMethodAudioTrackStopSpec);
    sMethodAudioTrackRelease = getJavaMethod(env, clazz, sMethodAudioTrackReleaseSpec);
    env->DeleteLocalRef(clazz);
}

AudioRenderer::AudioRenderer(JniCallbackHandler *handler, jobject jAudioTrack, JNIEnv* env) :
        mJniHandler(handler),
        mChannels(0),
        mSampleRate(0),
        mLayout(0),
        mFormat(AV_SAMPLE_FMT_NONE),
        instance(jAudioTrack) {
    mChannels = env->CallIntMethod(instance, sMethodAudioTrackChannelCount);
    if (0 < mChannels && mChannels < 9) {
        mSampleRate = env->CallIntMethod(instance, sMethodAudioTrackGetSampleRate);
        mFormat = AV_SAMPLE_FMT_S16;
        mLayout = sAndroidChannelLayout[mChannels - 1];
    } else {
        __android_log_print(ANDROID_LOG_ERROR, sTag,
                            "Cannot create swr because channel size is invalid: %d", mChannels);
    }
    env->CallVoidMethod(instance, sMethodAudioTrackPlay);
}

AudioRenderer::~AudioRenderer() {
}

int AudioRenderer::write(uint8_t *data, int len) {
    std::lock_guard<std::mutex> lk(mMutex);
    JNIEnv* env = mJniHandler->getEnv();
    if (!env) {
        return -1;
    }
    jbyteArray jArr = env->NewByteArray(len);
    if (jArr == NULL) {
        return AVERROR(ENOMEM);
    }
    jbyte *jSamples = env->GetByteArrayElements(jArr, NULL);
    if (jSamples == NULL) {
        return AVERROR(ENOMEM);
    }
    memcpy(jSamples, data, (size_t) len);
    env->ReleaseByteArrayElements(jArr, jSamples, 0);
    int ret = env->CallIntMethod(instance, sMethodAudioTrackWrite, jArr, 0, len);
    env->DeleteLocalRef(jArr);
    return ret;
}

int AudioRenderer::pause() {
    std::lock_guard<std::mutex> lk(mMutex);
    JNIEnv* env = mJniHandler->getEnv();
    if (!env) {
        return -1;
    }
    env->CallVoidMethod(instance, sMethodAudioTrackPause);
    return 0;
}

int AudioRenderer::play() {
    std::lock_guard<std::mutex> lk(mMutex);
    JNIEnv* env = mJniHandler->getEnv();
    if (!env) {
        return -1;
    }
    env->CallVoidMethod(instance, sMethodAudioTrackPlay);
    return 0;
}

int AudioRenderer::flush() {
    std::lock_guard<std::mutex> lk(mMutex);
    JNIEnv* env = mJniHandler->getEnv();
    if (!env) {
        return -1;
    }
    env->CallVoidMethod(instance, sMethodAudioTrackFlush);
    return 0;
}

int AudioRenderer::stop() {
    std::lock_guard<std::mutex> lk(mMutex);
    JNIEnv* env = mJniHandler->getEnv();
    if (!env) {
        return -1;
    }
    env->CallVoidMethod(instance, sMethodAudioTrackStop);
    return 0;
}

#define _log(...) __android_log_print(ANDROID_LOG_INFO, "VPlayer2Native", __VA_ARGS__);


int AudioRenderer::release(JNIEnv* env) {
    std::lock_guard<std::mutex> lk(mMutex);
    env->CallVoidMethod(instance, sMethodAudioTrackRelease);
    return 0;
}
