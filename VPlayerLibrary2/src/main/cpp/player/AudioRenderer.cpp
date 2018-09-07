#include "AudioRenderer.h"

#define SEC_TO_NS 1e9
#define STABILIZING_MAX_COUNTER 5

static const int64_t TIMESTAMP_STABILIZING_NS = 500 * (int64_t) 1e6;    // 500 ms in nanoseconds
static const int64_t TIMESTAMP_POLLING_NS = 20 * (int64_t) SEC_TO_NS;   // 20 secs to poll in ns

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
    sMethodAudioTrackGetTimestamp = getJavaMethod(env, clazz, sMethodAudioTrackGetTimestampSpec);
    sMethodAudioTrackGetLatency = getJavaMethod(env, clazz, sMethodAudioTrackGetLatencySpec);
    sMethodAudioTrackStop = getJavaMethod(env, clazz, sMethodAudioTrackStopSpec);
    sMethodAudioTrackRelease = getJavaMethod(env, clazz, sMethodAudioTrackReleaseSpec);
    env->DeleteLocalRef(clazz);

    // AudioTimestamp class
    const jclass tsClazz = env->FindClass(sAudioTimestampClassName);
    sMethodAudioTimeStampCtor = getJavaMethod(env, tsClazz, sMethodAudioTimestampCtorSpec);
    sFieldAudioTimestampFramePosition =
            getJavaField(env, tsClazz, sFieldAudioTimestampFrmPositionSpec);
    sFieldAudioTimestampNanoTime = getJavaField(env, tsClazz, sFieldAudioTimestampNanoTimeSpec);
    env->DeleteLocalRef(tsClazz);
}

AudioRenderer::AudioRenderer(JniCallbackHandler *handler, jobject jAudioTrack, JNIEnv* env) :
        mJniHandler(handler),
        mChannels(0),
        mSampleRate(0),
        mLayout(0),
        mFormat(AV_SAMPLE_FMT_NONE),
        instance(jAudioTrack),
        mFramesWritten(0),
        mUseTimestampApi(true),
        mLastFramePosition(0),
        mLastFrameTime(0),
        mLastLatencySec(0),
        mLastTimestampCheck(0),
        mTimestampStabilizing(true),
        mStabilizingCounter(0),
        mOldHeadTimeSmoothArr(NULL),
        mOldHeadTimeSmoothArrSize(0),
        mOldHeadTimeSmoothArrIndex(0) {
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
    if (mOldHeadTimeSmoothArr) {
        delete[] mOldHeadTimeSmoothArr;
    }
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
    mFramesWritten += ret / (numChannels() * av_get_bytes_per_sample(format()));
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

int AudioRenderer::release(JNIEnv* env) {
    std::lock_guard<std::mutex> lk(mMutex);
    env->CallVoidMethod(instance, sMethodAudioTrackRelease);
    return 0;
}

double AudioRenderer::getLatency() {
    return mLastLatencySec;
}

double AudioRenderer::updateLatency(bool force) {
    std::lock_guard<std::mutex> lk(mMutex);
    int64_t now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    long framePos = 0;
    long timestamp = 0;

    JNIEnv* env = mJniHandler->getEnv();
    if (!env) {
        return -1;
    }

    // Update latency if passed the timeout threshold either in stabilizing or polling mode
    int64_t threshold = mTimestampStabilizing ? TIMESTAMP_STABILIZING_NS : TIMESTAMP_POLLING_NS;
    if (now - mLastTimestampCheck > threshold || force) {
        mLastTimestampCheck = now;

        if (!mUseTimestampApi) {
            // Already confirmed this audio sample cannot use getTimestamp(), use old method
            mTimestampStabilizing = false;
            return getLatencyOldMethod();
        }

        // If the unable to getTimestamp, time or position is the same as before, clock is
        // stabilizing so wait shorter periods of time until getTimestamp is stabilized
        double latency = 0;
        bool needStabilization = !getTimeStamp(&framePos, &timestamp) || mLastFrameTime == timestamp
                           || timestamp <= 0 || mLastFramePosition == framePos
                           || mLastFramePosition < 0;
        if (!needStabilization) {
            // Calculate the latency compared to the data already written
            double headPos = framePos + (now - timestamp) * mSampleRate / SEC_TO_NS;
            latency = (mFramesWritten - headPos) / mSampleRate;

            // If latency is negative, it means it is stabilizing, might be after pause then play
            needStabilization |= latency < 0;
        }
        if (needStabilization) {
            mTimestampStabilizing = true;
            __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Audio timestamp is stabilizing");

            if (++mStabilizingCounter >= STABILIZING_MAX_COUNTER) {
                __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Taking too long to stabilize "
                        "AudioTrack.getTimestamp, switching to old method.");
                mUseTimestampApi = false;
                return getLatencyOldMethod();
            }
            return mLastLatencySec;
        }
        mStabilizingCounter = 0;
        mTimestampStabilizing = false;
        mLastFramePosition = framePos;
        mLastFrameTime = timestamp;
        mLastLatencySec = latency;
        __android_log_print(ANDROID_LOG_VERBOSE, sTag,
                            "Update audio latency [getTimestamp()] %lf ms", mLastLatencySec * 1000);
    }
    return mLastLatencySec;
}

bool AudioRenderer::getTimeStamp(long *outFramePosition, long *outNanoTime) {
    JNIEnv* env = mJniHandler->getEnv();
    if (!env) {
        return false;
    }
    const jclass clazz = env->FindClass(sAudioTimestampClassName);
    jobject timestamp = env->NewObject(clazz, sMethodAudioTimeStampCtor);
    env->DeleteLocalRef(clazz);
    if (!timestamp) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to create audio timestamp class!!");
        return false;
    }
    jboolean res = env->CallBooleanMethod(instance, sMethodAudioTrackGetTimestamp, timestamp);
    if (res == JNI_TRUE) {
        if (outFramePosition) {
            *outFramePosition = env->GetLongField(timestamp, sFieldAudioTimestampFramePosition);
        }
        if (outNanoTime) {
            *outNanoTime = env->GetLongField(timestamp, sFieldAudioTimestampNanoTime);
        }
    }
    env->DeleteLocalRef(timestamp);
    return res == JNI_TRUE;
}

double AudioRenderer::getLatencyOldMethod() {
    JNIEnv* env = mJniHandler->getEnv();
    if (!env) {
        return -1;
    }
    mLastLatencySec = (double) env->CallIntMethod(instance, sMethodAudioTrackGetLatency) / 1000.0;
    __android_log_print(ANDROID_LOG_VERBOSE, sTag,
                        "Update audio latency [reflect:getLatency] %lf ms", mLastLatencySec * 1000);
    return mLastLatencySec;
}
