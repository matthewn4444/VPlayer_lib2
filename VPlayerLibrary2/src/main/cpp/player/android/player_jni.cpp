#include "../Player.h"
#include "JniCallbackHandler.h"
#include "JniVideoRenderer.h"

#define PLAYER_JAVA_PKG com_matthewn4444_vplayerlibrary2_VPlayer2NativeController
#define EXPORT_PLAYER(name) JAVA_EXPORT_NAME1(name,PLAYER_JAVA_PKG)

static const char* sTag = "NativeJniPLayer";

static const JavaField sNativePlayerSpec = {"mNativePlayerInstance", "J"};
static jfieldID sNativePlayerInstance;
static const JavaField sNativeJniHandlerSpec = {"mNativeJniHandlerInstance", "J"};
static jfieldID sNativeJniHandler;
static const JavaField sNativeJniVideoRendererSpec = {"mNativeJniVideoRendererInstance", "J"};
static jfieldID sNativeJniVideoRenderer;

extern "C" {
JNIEXPORT void EXPORT_PLAYER(nativeInit) (JNIEnv *env, jclass type) {
    // Video Controller class
    const jclass clazz = env->FindClass(sControllerClassName);
    sNativePlayerInstance = getJavaField(env, clazz, sNativePlayerSpec);
    sNativeJniHandler = getJavaField(env, clazz, sNativeJniHandlerSpec);
    sNativeJniVideoRenderer = getJavaField(env, clazz, sNativeJniVideoRendererSpec);
    env->DeleteLocalRef(clazz);

    JniCallbackHandler::initJni(env);
    AudioRenderer::initJni(env);
}

JNIEXPORT jboolean EXPORT_PLAYER(initPlayer) (JNIEnv *env, jobject instance) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (!player) {
        // Create the callback handler
        JniCallbackHandler* handler = new JniCallbackHandler(env, instance);
        if (!handler->makeInstanceGlobalRef(env)) {
            __android_log_print(ANDROID_LOG_FATAL, sTag, "Cannot get new global ref of instance");
            return JNI_FALSE;
        }
        setPtr(env, instance, sNativeJniHandler, handler);

        // Create the player
        player = new Player();
        player->setCallback(handler);
        setPtr(env, instance, sNativePlayerInstance, player);
    } else {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Creating new player when it already exists.");
    }
    return JNI_TRUE;
}

JNIEXPORT void EXPORT_PLAYER(destroyPlayer) (JNIEnv *env, jobject instance) {
    double begin = Clock::now();
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    JniCallbackHandler* handler = getPtr<JniCallbackHandler>(env, instance, sNativeJniHandler);
    if (player) {
        setPtr(env, instance, sNativePlayerInstance, NULL);
        delete player;

        if (handler) {
            handler->deleteInstanceGlobalRef(env);
            delete handler;
            setPtr(env, instance, sNativeJniHandler, NULL);
        }
    }

    JniVideoRenderer* vRenderer = getPtr<JniVideoRenderer>(env, instance, sNativeJniVideoRenderer);
    if (vRenderer) {
        setPtr(env, instance, sNativeJniVideoRenderer, NULL);
        delete vRenderer;
    }
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Video player is fully destroyed in %lf ms",
                        (Clock::now() - begin) * 1000);
}

JNIEXPORT void EXPORT_PLAYER(surfaceCreated) (JNIEnv *env, jobject instance, jobject vSurface,
                                              jobject sSurface) {
    JniVideoRenderer* vRenderer = getPtr<JniVideoRenderer>(env, instance, sNativeJniVideoRenderer);
    if (!vRenderer) {
        vRenderer = new JniVideoRenderer();
        setPtr(env, instance, sNativeJniVideoRenderer, vRenderer);

        Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
        if (player) {
            player->setVideoRenderer(vRenderer);
        }
    }
    vRenderer->onSurfaceCreated(env, vSurface, sSurface);
}

JNIEXPORT void EXPORT_PLAYER(surfaceDestroyed) (JNIEnv *env, jobject instance) {
    // When destroying surface, make sure no frames are held as locked in the video stream
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player) {
        player->invalidateVideoFrame();
    }

    JniVideoRenderer* vRenderer = getPtr<JniVideoRenderer>(env, instance, sNativeJniVideoRenderer);
    if (vRenderer) {
        vRenderer->onSurfaceDestroyed();
    }
}

JNIEXPORT void EXPORT_PLAYER(nativeSetSubtitleFrameSize) (JNIEnv *env, jobject instance,
                                                          jint width, jint height) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player) {
        player->setSubtitleFrameSize(width, height);
    }
}

JNIEXPORT void EXPORT_PLAYER(nativeSetDefaultSubtitleFont) (JNIEnv *env, jobject instance,
                                                            jstring jfontPath,
                                                            jstring jfontFamily) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player) {
        const char *fontPath = env->GetStringUTFChars(jfontPath, 0);
        const char *fontFamily = env->GetStringUTFChars(jfontFamily, 0);
        player->setDefaultSubtitleFont(fontPath, fontFamily);
        env->ReleaseStringUTFChars(jfontPath, fontPath);
        env->ReleaseStringUTFChars(jfontFamily, fontFamily);
    }
}

JNIEXPORT void JNICALL EXPORT_PLAYER(nativeRenderLastFrame) (JNIEnv *env, jobject instance) {
    JniVideoRenderer* vRenderer = getPtr<JniVideoRenderer>(env, instance, sNativeJniVideoRenderer);
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (vRenderer && player && player->isPaused()) {
        vRenderer->renderLastFrame();
    }
}

JNIEXPORT void JNICALL EXPORT_PLAYER(remeasureAudioLatency) (JNIEnv *env, jobject instance) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player != NULL) {
        player->remeasureAudioLatency();
    }
}

JNIEXPORT jboolean JNICALL EXPORT_PLAYER(nativeOpen) (JNIEnv *env, jobject instance,
                                                      jstring streamFileUrl) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player != NULL) {
        const char* url = env->GetStringUTFChars(streamFileUrl, 0);
        bool ret = player->openVideo(url);
        env->ReleaseStringUTFChars(streamFileUrl, url);
        return (jboolean) ret;
    }
    return JNI_FALSE;
}

JNIEXPORT void JNICALL EXPORT_PLAYER(nativeSeek) (JNIEnv *env, jobject instance,
                                                  jlong positionMill) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player) {
        player->seek(positionMill);
    }
}

JNIEXPORT void JNICALL EXPORT_PLAYER(nativeFrameStep) (JNIEnv *env, jobject instance) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player) {
        player->stepNextFrame();
    }
}

JNIEXPORT void JNICALL EXPORT_PLAYER(nativePlay) (JNIEnv *env, jobject instance) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player && player->isPaused()) {
        player->togglePlayback();
    }
}

JNIEXPORT void JNICALL EXPORT_PLAYER(nativePause) (JNIEnv *env, jobject instance) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    if (player && !player->isPaused()) {
        player->togglePlayback();
    }
}

JNIEXPORT jboolean JNICALL EXPORT_PLAYER(nativeIsPaused) (JNIEnv *env, jobject instance) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    return (jboolean) (player && player->isPaused() ? JNI_TRUE : JNI_FALSE);
}

JNIEXPORT jlong JNICALL EXPORT_PLAYER(nativeGetDurationMill) (JNIEnv *env, jobject instance) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    return player ? player->getDuration() : 0;
}

JNIEXPORT jlong JNICALL EXPORT_PLAYER(nativeGetPlaybackMill) (JNIEnv *env, jobject instance) {
    Player* player = getPtr<Player>(env, instance, sNativePlayerInstance);
    return player && !isnan((long) player->getMasterClock())
            ? (long) (player->getMasterClock()->getPts() * 1000) : -1;
}
}
