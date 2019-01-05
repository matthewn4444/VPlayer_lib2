#include "JniCallbackHandler.h"

#define sTag "JniCallbackHandler"

static const char* sCannotAllocMetadataError = "Cannot send metadata because cannot allocate map";

void JniCallbackHandler::initJni(JNIEnv *env) {
    // Video Controller class
    const jclass clazz = env->FindClass(sControllerClassName);
    sMethodNativeError = getJavaMethod(env, clazz, sMethodNativeErrorSpec);
    sMethodMetadataReady = getJavaMethod(env, clazz, sMethodMetadataReadySpec);
    sMethodCreateAudioTrack = getJavaMethod(env, clazz, sMethodCreateAudioTrackSpec);
    sMethodStreamReady = getJavaMethod(env, clazz, sMethodStreamReadySpec);
    sMethodStreamFinished = getJavaMethod(env, clazz, sMethodStreamFinishedSpec);
    sMethodProgressChanged = getJavaMethod(env, clazz, sMethodProgressChangedSpec);
    sMethodPlaybackChanged = getJavaMethod(env, clazz, sMethodPlaybackChangedSpec);
    env->DeleteLocalRef(clazz);

    // Hashmap class
    const jclass hashMapClass = env->FindClass(sHashMapClassName);
    sMethodHashMapDefaultConstructor = getJavaMethod(env, hashMapClass, sMethodDefaultConstSpec);
    sMethodMapPut = getJavaMethod(env, hashMapClass, sMethodMapPutSpec);
    env->DeleteLocalRef(hashMapClass);
}

const static jobject getHashMapFromAVDictionary(JNIEnv* env, AVDictionary* dictionary,
                                                const jclass& hashmapClass) {
    AVDictionaryEntry* t = NULL;
    jobject map, key, value;
    map = env->NewObject(hashmapClass, sMethodHashMapDefaultConstructor);
    if (map == NULL) {
        return NULL;
    }
    while ((t = av_dict_get(dictionary, "", t, AV_DICT_IGNORE_SUFFIX))) {
        if ((key = env->NewStringUTF(t->key)) == NULL
            || (value = env->NewStringUTF(t->value)) == NULL) {
            // All previous variables will be gc eventually by Java
            env->DeleteLocalRef(map);
            return NULL;
        }
        env->CallObjectMethod(map, sMethodMapPut, key, value);
    }
    return map;
}

JniCallbackHandler::JniCallbackHandler(JNIEnv *env, jobject instance) :
        JniHelper(env, instance),
        mAudioRenderer(NULL) {
}

JniCallbackHandler::~JniCallbackHandler() {
}

void JniCallbackHandler::deleteInstanceGlobalRef(JNIEnv *env) {
    JniHelper::deleteInstanceGlobalRef(env);
    std::lock_guard<std::mutex> lk(mMutex);
    if (mAudioRenderer) {
        if (env) {
            mAudioRenderer->release(env);
            env->DeleteGlobalRef(mAudioRenderer->instance);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, sTag,
                                "AudioTrack cannot be released because no jni env, will leak!");
        }
        delete mAudioRenderer;
        mAudioRenderer = NULL;
    }
}

void JniCallbackHandler::onError(int errorCode, const char * tag, const char * message) {
    JNIEnv* env = getEnv();
    std::lock_guard<std::mutex> lk(mMutex);

    char avmessage[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(avmessage, AV_ERROR_MAX_STRING_SIZE, errorCode);
    const char* errorMsg = avmessage;

    // If there is no message, get it from the ffmpeg error code (if it is)
    if (!message) {
        __android_log_print(ANDROID_LOG_ERROR, tag ? tag : sTag, "%s [ %d ]", avmessage, errorCode);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, tag ? tag : sTag, "%s: %s [ %d ]", message,
                            avmessage, errorCode);
        errorMsg = message;
    }

    // Send error to Java
    jstring s = env->NewStringUTF(errorMsg);
    if (s == NULL) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Cannot allocate memory to send error string");
    }
    env->CallVoidMethod(mInstance, sMethodNativeError, errorCode, s);
}

void JniCallbackHandler::onMetadataReady(AVDictionary* generalProp,
                                         AVDictionary** vPropList, size_t vCount,
                                         AVDictionary** aPropList, size_t aCount,
                                         AVDictionary** sPropList, size_t sCount) {
    JNIEnv* env = getEnv();
    std::lock_guard<std::mutex> lk(mMutex);
    int k = 1 /* generalProp */, i;
    bool error;
    jobject map;
    const jsize size = (jint) (1 /* generalProp */ + vCount + aCount + sCount);
    const jclass hashMapCls = env->FindClass(sHashMapClassName);
    jobjectArray retList = env->NewObjectArray(size, hashMapCls, NULL);

    if (!(error = (map = getHashMapFromAVDictionary(env, generalProp, hashMapCls)) == NULL)) {
        env->SetObjectArrayElement(retList, 0, map);
    }
    for (i = 0; i < !error && vCount; i++) {
        if (!(error = (map = getHashMapFromAVDictionary(env, vPropList[i], hashMapCls)) == NULL)) {
            env->SetObjectArrayElement(retList, k, map);
            k++;
        } else {
            break;
        }
    }
    for (i = 0; i < !error && aCount; i++) {
        if (!(error = (map = getHashMapFromAVDictionary(env, aPropList[i], hashMapCls)) == NULL)) {
            env->SetObjectArrayElement(retList, k, map);
            k++;
        } else {
            break;
        }
    }
    for (i = 0; i < !error && sCount; i++) {
        if (!(error = (map = getHashMapFromAVDictionary(env, sPropList[i], hashMapCls)) == NULL)) {
            env->SetObjectArrayElement(retList, k, map);
            k++;
        } else {
            break;
        }
    }
    if (error) {
        // All previous maps will be gc eventually by Java
        __android_log_print(ANDROID_LOG_ERROR, sTag, "%s", sCannotAllocMetadataError);
        onError(-1, sTag, sCannotAllocMetadataError);
    } else {
        env->CallVoidMethod(mInstance, sMethodMetadataReady, retList);
    }
    env->DeleteLocalRef(hashMapCls);
}

void JniCallbackHandler::onStreamReady() {
    JNIEnv* env = getEnv();
    std::lock_guard<std::mutex> lk(mMutex);
    env->CallVoidMethod(mInstance, sMethodStreamReady);
}

void JniCallbackHandler::onStreamFinished() {
    JNIEnv* env = getEnv();
    std::lock_guard<std::mutex> lk(mMutex);
    env->CallVoidMethod(mInstance, sMethodStreamFinished);
}

void JniCallbackHandler::onProgressChanged(long currentMs, long durationMs) {
    JNIEnv* env = getEnv();
    std::lock_guard<std::mutex> lk(mMutex);
    env->CallVoidMethod(mInstance, sMethodProgressChanged, currentMs, durationMs);
}

void JniCallbackHandler::onPlaybackChanged(bool playing) {
    JNIEnv* env = getEnv();
    std::lock_guard<std::mutex> lk(mMutex);
    env->CallVoidMethod(mInstance, sMethodPlaybackChanged, playing);
}

IAudioRenderer *JniCallbackHandler::createAudioRenderer(AVCodecContext *context) {
    JNIEnv* env = getEnv();
    std::lock_guard<std::mutex> lk(mMutex);
    jobject audioTrack = env->CallObjectMethod(mInstance, sMethodCreateAudioTrack,
                                               context->sample_rate, context->channels);
    if (audioTrack == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, sTag,
                            "Cannot create audio track because java failed to return it");
        return NULL;
    }

    // Delete previous track if exists
    if (mAudioRenderer) {
        env->DeleteGlobalRef(mAudioRenderer->instance);
        delete mAudioRenderer;
        mAudioRenderer = NULL;
    }

    // Try to create a global reference
    jobject gAudioTrack = env->NewGlobalRef(audioTrack);
    env->DeleteLocalRef(audioTrack);
    if (!gAudioTrack) {
        __android_log_print(ANDROID_LOG_ERROR, sTag,
                            "Cannot create jni global reference to audio track");
        return NULL;
    }
    AudioRenderer* renderer = new AudioRenderer(this, gAudioTrack, env);
    if (0 >= renderer->numChannels() && renderer->numChannels() > 8) {
        // Renderer is invalid
        env->DeleteGlobalRef(gAudioTrack);
        delete renderer;
        __android_log_print(ANDROID_LOG_ERROR, sTag,
                            "Cannot get audio renderer because it is invalid");
        return NULL;
    }
    return mAudioRenderer = renderer;
}

bool JniCallbackHandler::onThreadStart() {
    return attachToThisThread() != NULL;
}

void JniCallbackHandler::onThreadEnd() {
    detachFromThisThread();
}
