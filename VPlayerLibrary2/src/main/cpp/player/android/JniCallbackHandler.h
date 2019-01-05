#ifndef VPLAYER_LIB2_JNIERRORHANDLER_H
#define VPLAYER_LIB2_JNIERRORHANDLER_H

extern "C" {
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
}

#include "JniHelper.h"
#include "AudioRenderer.h"
#include "../IPlayerCallback.h"

static const char* sControllerClassName = JAVA_PKG_PATH "/VPlayer2NativeController";

// HashMap
static const char* sHashMapClassName = "java/util/HashMap";
static const JavaMethod sMethodMapPutSpec = {"put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"};
static jmethodID sMethodMapPut;
static jmethodID sMethodHashMapDefaultConstructor;

static const JavaMethod sMethodDefaultConstSpec = {"<init>", "()V"};
static const JavaMethod sMethodNativeErrorSpec = {"nativeStreamError", "(ILjava/lang/String;)V"};
static const JavaMethod sMethodMetadataReadySpec = {"nativeMetadataReady", "([Ljava/util/Map;)V"};
static const JavaMethod sMethodCreateAudioTrackSpec = {"nativeCreateAudioTrack", "(II)Landroid/media/AudioTrack;"};
static const JavaMethod sMethodStreamReadySpec = {"nativeStreamReady", "()V"};
static const JavaMethod sMethodStreamFinishedSpec = {"nativeStreamFinished", "()V"};
static const JavaMethod sMethodProgressChangedSpec = {"nativeProgressChanged", "(JJ)V"};
static const JavaMethod sMethodPlaybackChangedSpec = {"nativePlaybackChanged", "(Z)V"};
static jmethodID sMethodNativeError;
static jmethodID sMethodMetadataReady;
static jmethodID sMethodCreateAudioTrack;
static jmethodID sMethodStreamReady;
static jmethodID sMethodStreamFinished;
static jmethodID sMethodProgressChanged;
static jmethodID sMethodPlaybackChanged;

class AudioRenderer;

class JniCallbackHandler : public IPlayerCallback, public JniHelper {
public:
    static void initJni(JNIEnv *env);

    JniCallbackHandler(JNIEnv *env, jobject instance);
    virtual ~JniCallbackHandler();

    void deleteInstanceGlobalRef(JNIEnv* env) override;

    void onError(int, const char*, const char*) override;
    void onMetadataReady(AVDictionary*, AVDictionary**, size_t, AVDictionary**, size_t,
                         AVDictionary**, size_t) override;
    void onStreamReady() override;
    void onStreamFinished() override;
    void onProgressChanged(long currentMs, long durationMs);
    void onPlaybackChanged(bool playing) override;

    IAudioRenderer *createAudioRenderer(AVCodecContext *context) override;

    bool onThreadStart() override;
    void onThreadEnd() override;

private:
    AudioRenderer* mAudioRenderer;
};

#endif //VPLAYER_LIB2_JNIERRORHANDLER_H
