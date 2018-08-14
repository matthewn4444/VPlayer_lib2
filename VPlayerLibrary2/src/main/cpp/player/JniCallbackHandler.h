#ifndef VPLAYER_LIB2_JNIERRORHANDLER_H
#define VPLAYER_LIB2_JNIERRORHANDLER_H

extern "C" {
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
}

typedef struct {
    const char* name;
    const char* signature;
} JavaField, JavaMethod;

#include <map>
#include <thread>
#include <mutex>
#include <android/log.h>
#include <jni.h>
#include "IPlayerCallback.h"
#include "AudioRenderer.h"

#define JAVA_PKG_PATH "com/matthewn4444/vplayerlibrary2"
static const char* sControllerClassName = JAVA_PKG_PATH "/VPlayer2NativeController";

// HashMap
static const char* sHashMapClassName = "java/util/HashMap";
static JavaMethod sMethodMapPutSpec = {"put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"};
static jmethodID sMethodMapPut;
static jmethodID sMethodHashMapDefaultConstructor;

static JavaMethod sMethodDefaultConstSpec = {"<init>", "()V"};
static const JavaMethod sMethodNativeErrorSpec = {"nativeStreamError", "(ILjava/lang/String;)V"};
static const JavaMethod sMethodMetadataReadySpec = {"nativeMetadataReady", "([Ljava/util/Map;)V"};
static const JavaMethod sMethodCreateAudioTrackSpec = {"nativeCreateAudioTrack", "(II)Landroid/media/AudioTrack;"};
static const JavaMethod sMethodStreamReadySpec = {"nativeStreamReady", "()V"};
static const JavaMethod sMethodStreamFinishedSpec = {"nativeStreamFinished", "()V"};
static jmethodID sMethodNativeError;
static jmethodID sMethodMetadataReady;
static jmethodID sMethodCreateAudioTrack;
static jmethodID sMethodStreamReady;
static jmethodID sMethodStreamFinished;

jfieldID getJavaField(JNIEnv *env, jclass clazz, JavaField field);
jfieldID getStaticJavaField(JNIEnv *env, jclass clazz, JavaField field);
jmethodID getJavaMethod(JNIEnv *env, jclass clazz, JavaMethod method);
jmethodID getStaticJavaMethod(JNIEnv *env, jclass clazz, JavaMethod method);

class AudioRenderer;

class JniCallbackHandler : public IPlayerCallback {
public:
    static void initJni(JNIEnv *env);

    JniCallbackHandler(JavaVM* vm, jobject instance);
    ~JniCallbackHandler();

    jobject makeInstanceGlobalRef(JNIEnv* env);
    void deleteInstanceGlobalRef(JNIEnv* env);

    JNIEnv* attachToThisThread();
    void detachFromThisThread();

    JNIEnv* getEnv();

    void onError(int, const char*, const char*) override;
    void onMetadataReady(AVDictionary*, AVDictionary**, size_t, AVDictionary**, size_t,
                         AVDictionary**, size_t) override;
    void onStreamReady() override;
    void onStreamFinished() override;

    IAudioRenderer *createAudioRenderer(AVCodecContext *context) override;

    bool onThreadStart() override;
    void onThreadEnd() override;
private:
    AudioRenderer* mAudioRenderer;
    std::map<std::thread::id, JNIEnv*> mEnvLookup;
    JavaVM* mJavaVM;
    jobject mInstance;
    std::mutex mMutex;
    bool mHasGlobalRef;
};

#endif //VPLAYER_LIB2_JNIERRORHANDLER_H
