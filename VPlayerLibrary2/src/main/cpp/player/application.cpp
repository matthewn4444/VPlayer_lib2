#include <android/log.h>
#include "Player.h"
#include "JniCallbackHandler.h"
#include "JniVideoRenderer.h"

#include "AudioRenderer.h"

#define _log(...) __android_log_print(ANDROID_LOG_INFO, "VPlayer2Native", __VA_ARGS__);

#define SDL_JAVA_PKG com_matthewn4444_vplayerlibrary2_VPlayer2NativeController

#define JAVA_EXPORT_NAME2(name,package) JNICALL Java_##package##_##name
#define JAVA_EXPORT_NAME1(name,package) JAVA_EXPORT_NAME2(name,package)
#define JAVA_EXPORT_NAME(name) JAVA_EXPORT_NAME1(name,SDL_JAVA_PKG)

static const char* sTag = "NativeJNILayer";

static JavaVM* sJavaVM = NULL;

static const JavaField sNativePlayerSpec = {"mNativePlayerInstance", "J"};
static jfieldID sNativeInstance;
static const JavaField sNativeJniHandlerSpec = {"mNativeJniHandlerInstance", "J"};
static jfieldID sNativeJniHandler;
static const JavaField sNativeJniVideoRendererSpec = {"mNativeJniVideoRendererInstance", "J"};
static jfieldID sNativeJniVideoRenderer;

Player* getPlayerPtr(JNIEnv *env, jobject thiz) {
    return (Player*) env->GetLongField(thiz, sNativeInstance);
}

void setPlayerPtr(JNIEnv *env, jobject instance, Player *player) {
    env->SetLongField(instance, sNativeInstance, (jlong) player);
}

JniCallbackHandler* getHandlerPtr(JNIEnv *env, jobject thiz) {
    return (JniCallbackHandler*) env->GetLongField(thiz, sNativeJniHandler);
}

void setHandlerPtr(JNIEnv *env, jobject instance, JniCallbackHandler *handler) {
    env->SetLongField(instance, sNativeJniHandler, (jlong) handler);
}

JniVideoRenderer* getVideoRendererPtr(JNIEnv *env, jobject thiz) {
    return (JniVideoRenderer*) env->GetLongField(thiz, sNativeJniVideoRenderer);
}

void setVideoRendererPtr(JNIEnv *env, jobject instance, JniVideoRenderer *videoRenderer) {
    env->SetLongField(instance, sNativeJniVideoRenderer, (jlong) videoRenderer);
}

extern "C" JNIEXPORT void JAVA_EXPORT_NAME(nativeInit) (JNIEnv *env, jclass type) {
    if (sJavaVM == NULL && env->GetJavaVM(&sJavaVM) < 0) {
        __android_log_print(ANDROID_LOG_FATAL, sTag, "Failed to get JNI Java VM for init");
        return;
    }

    // Video Controller class
    const jclass clazz = env->FindClass(sControllerClassName);
    sNativeInstance = getJavaField(env, clazz, sNativePlayerSpec);
    sNativeJniHandler = getJavaField(env, clazz, sNativeJniHandlerSpec);
    sNativeJniVideoRenderer = getJavaField(env, clazz, sNativeJniVideoRendererSpec);
    env->DeleteLocalRef(clazz);

    JniCallbackHandler::initJni(env);
    AudioRenderer::initJni(env);
}

extern "C" JNIEXPORT jboolean JAVA_EXPORT_NAME(initPlayer) (JNIEnv *env, jobject instance) {
    Player* player = getPlayerPtr(env, instance);
    JniCallbackHandler* handler = getHandlerPtr(env, instance);
    JniVideoRenderer* vRenderer = getVideoRendererPtr(env, instance);
    if (!player) {
        if (!sJavaVM) {
            return JNI_FALSE;
        }

        // Create the callback handler
        if (handler) {
            delete handler;
            setHandlerPtr(env, instance, NULL);
        }
        handler = new JniCallbackHandler(sJavaVM, instance);
        if (!handler->makeInstanceGlobalRef(env)) {
            __android_log_print(ANDROID_LOG_FATAL, sTag, "Cannot get new global ref of instance");
            return JNI_FALSE;
        }
        setHandlerPtr(env, instance, handler);
        // Create the player
        player = new Player();
        player->setCallback(handler);
        if (vRenderer) {
            player->setVideoRenderer(vRenderer);
        }
        setPlayerPtr(env, instance, player);
    } else {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Creating new player when it already exists.");
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JAVA_EXPORT_NAME(destroyPlayer) (JNIEnv *env, jobject instance) {
    double begin = Clock::now();
    Player* player = getPlayerPtr(env, instance);
    JniCallbackHandler* handler = getHandlerPtr(env, instance);
    if (player) {
        setPlayerPtr(env, instance, NULL);
        delete player;

        if (handler) {
            handler->deleteInstanceGlobalRef(env);
            delete handler;
            setHandlerPtr(env, instance, NULL);
        }
    }

    JniVideoRenderer* vRenderer = getVideoRendererPtr(env, instance);
    if (vRenderer) {
        setVideoRendererPtr(env, instance, NULL);
        delete vRenderer;
    }
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Video player is fully destroyed in %lf ms",
                        (Clock::now() - begin) * 1000);
}

extern "C" JNIEXPORT void JAVA_EXPORT_NAME(surfaceCreated) (JNIEnv *env, jobject instance,
                                                    jobject vSurface, jobject sSurface) {
    JniVideoRenderer* vRenderer = getVideoRendererPtr(env, instance);
    if (!vRenderer) {
        vRenderer = new JniVideoRenderer();
        setVideoRendererPtr(env, instance, vRenderer);

        Player* player = getPlayerPtr(env, instance);
        if (player) {
            player->setVideoRenderer(vRenderer);
        }
    }
    vRenderer->onSurfaceCreated(env, vSurface, sSurface);
}

extern "C" JNIEXPORT void JAVA_EXPORT_NAME(surfaceDestroyed) (JNIEnv *env, jobject instance) {
    // When destroying surface, make sure no frames are held as locked in the video stream
    Player* player = getPlayerPtr(env, instance);
    if (player) {
        player->invalidateVideoFrame();
    }

    JniVideoRenderer* vRenderer = getVideoRendererPtr(env, instance);
    if (vRenderer) {
        vRenderer->onSurfaceDestroyed();
    }
}

extern "C" JNIEXPORT void JAVA_EXPORT_NAME(nativeSetSubtitleFrameSize) (JNIEnv *env,
                                                                        jobject instance,
                                                                        jint width, jint height) {
    Player* player = getPlayerPtr(env, instance);
    if (player) {
        player->setSubtitleFrameSize(width, height);
    }
}

extern "C" JNIEXPORT void JAVA_EXPORT_NAME(nativeSetDefaultSubtitleFont) (JNIEnv *env,
                                                                          jobject instance,
                                                                          jstring jfontPath,
                                                                          jstring jfontFamily) {
    Player* player = getPlayerPtr(env, instance);
    if (player) {
        const char *fontPath = env->GetStringUTFChars(jfontPath, 0);
        const char *fontFamily = env->GetStringUTFChars(jfontFamily, 0);
        player->setDefaultSubtitleFont(fontPath, fontFamily);
        env->ReleaseStringUTFChars(jfontPath, fontPath);
        env->ReleaseStringUTFChars(jfontFamily, fontFamily);
    }
}

extern "C" JNIEXPORT void JNICALL JAVA_EXPORT_NAME(nativeRenderLastFrame) (JNIEnv *env,
                                                                           jobject instance) {
    JniVideoRenderer* vRenderer = getVideoRendererPtr(env, instance);
    Player* player = getPlayerPtr(env, instance);
    if (vRenderer && player && player->isPaused()) {
        vRenderer->renderLastFrame();
    }
}

extern "C" JNIEXPORT void JNICALL JAVA_EXPORT_NAME(remeasureAudioLatency) (JNIEnv *env,
                                                                           jobject instance) {
    Player* player = getPlayerPtr(env, instance);
    if (player != NULL) {
        player->remeasureAudioLatency();
    }
}

extern "C" JNIEXPORT jboolean JNICALL JAVA_EXPORT_NAME(nativeOpen) (JNIEnv *env, jobject instance,
                                                                   jstring streamFileUrl) {
    Player* player = getPlayerPtr(env, instance);
    if (player != NULL) {
        const char* url = env->GetStringUTFChars(streamFileUrl, 0);
        bool ret = player->openVideo(url);
        env->ReleaseStringUTFChars(streamFileUrl, url);
        return (jboolean) ret;
    }
    return JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL JAVA_EXPORT_NAME(nativeSeek) (JNIEnv *env, jobject instance,
                                                                jlong positionMill) {
    Player* player = getPlayerPtr(env, instance);
    if (player) {
        player->seek(positionMill);
    }
}

extern "C" JNIEXPORT void JNICALL JAVA_EXPORT_NAME(nativeFrameStep) (JNIEnv *env,
                                                                     jobject instance) {
    Player* player = getPlayerPtr(env, instance);
    if (player) {
        player->stepNextFrame();
    }
}

extern "C" JNIEXPORT void JNICALL JAVA_EXPORT_NAME(nativePlay) (JNIEnv *env, jobject instance) {
    Player* player = getPlayerPtr(env, instance);
    if (player && player->isPaused()) {
        player->togglePlayback();
    }
}

extern "C" JNIEXPORT void JNICALL JAVA_EXPORT_NAME(nativePause) (JNIEnv *env, jobject instance) {
    Player* player = getPlayerPtr(env, instance);
    if (player && !player->isPaused()) {
        player->togglePlayback();
    }
}

extern "C" JNIEXPORT jboolean JNICALL JAVA_EXPORT_NAME(nativeIsPaused) (JNIEnv *env,
                                                                        jobject instance) {
    Player* player = getPlayerPtr(env, instance);
    return (jboolean) (player && player->isPaused() ? JNI_TRUE : JNI_FALSE);
}

extern "C" JNIEXPORT jlong JNICALL JAVA_EXPORT_NAME(nativeGetDurationMill) (JNIEnv *env,
                                                                        jobject instance) {
    Player* player = getPlayerPtr(env, instance);
    return player ? player->getDuration() : 0;
}


extern "C" JNIEXPORT jlong JNICALL JAVA_EXPORT_NAME(nativeGetPlaybackMill) (JNIEnv *env,
                                                                            jobject instance) {
    Player* player = getPlayerPtr(env, instance);
    return player && !isnan((long) player->getMasterClock())
            ? (long) (player->getMasterClock()->getPts() * 1000) : -1;
}
