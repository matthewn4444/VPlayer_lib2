#include "../ASSRenderer.h"
#include "JniHelper.h"

#define EXPORT_RENDERER(name) JAVA_EXPORT_NAME1(name,com_matthewn4444_vplayerlibrary2_ASSRenderer)
#define EXPORT_TRACK(name) JAVA_EXPORT_NAME1(name,com_matthewn4444_vplayerlibrary2_ASSTrack)

static const char* sTag = "NativeJNISubtitles";

// Renderer Class
static const JavaField sNativeRendererSpec = {"mRendererInstance", "J"};
static jfieldID sNativeRendererInstance;

// ASSBitmap Class
static JavaMethod sMethodASSBitmapCtorSpec = {"<init>", "(IIII[BZI)V"};
static jclass gClassASSBitmap = NULL;
static jmethodID sMethodASSBitmapCtor;

// ASSFrame Class
static JavaMethod sMethodASSFrameCtorSpec = {"<init>", "(J[L" JAVA_PKG_PATH "/ASSBitmap;)V"};
static jclass gClassASSFrame = NULL;
static jmethodID sMethodASSFrameCtor;

extern "C" {
JNIEXPORT void EXPORT_RENDERER(nativeInit) (JNIEnv *env, jclass type) {
    // Renderer Class
    const jclass clazz = env->FindClass(JAVA_PKG_PATH"/ASSRenderer");
    sNativeRendererInstance = getJavaField(env, clazz, sNativeRendererSpec);
    env->DeleteLocalRef(clazz);

    // ASSBitmap class
    const jclass imgClazz = env->FindClass(JAVA_PKG_PATH"/ASSBitmap");
    sMethodASSBitmapCtor = getJavaMethod(env, imgClazz, sMethodASSBitmapCtorSpec);
    env->DeleteLocalRef(imgClazz);

    // ASSFrame class
    const jclass frameClazz = env->FindClass(JAVA_PKG_PATH"/ASSFrame");
    sMethodASSFrameCtor = getJavaMethod(env, frameClazz, sMethodASSFrameCtorSpec);
    env->DeleteLocalRef(frameClazz);
}

JNIEXPORT jboolean JNICALL EXPORT_RENDERER(initRenderer) (JNIEnv *env, jobject instance) {
    auto* renderer = getPtr<ASSRenderer>(env, instance, sNativeRendererInstance);
    if (!renderer) {
        renderer = new ASSRenderer();
        if (renderer->getError() < 0) {
            delete renderer;
            return JNI_FALSE;
        }
        setPtr(env, instance, sNativeRendererInstance, renderer);
    } else {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Creating new renderer when it already exists");
    }
    return JNI_TRUE;
}

JNIEXPORT void JNICALL EXPORT_RENDERER(setSize) (JNIEnv *env, jobject instance, jint width,
                                                 jint height) {
    auto* renderer = getPtr<ASSRenderer>(env, instance, sNativeRendererInstance);
    if (renderer) {
        renderer->setSize(width, height);
    }
}

JNIEXPORT void JNICALL EXPORT_RENDERER(setDefaultFont) (JNIEnv *env, jobject instance,
                                                        jstring jfontPath, jstring jfontFamily) {
    auto* renderer = getPtr<ASSRenderer>(env, instance, sNativeRendererInstance);
    if (renderer) {
        const char *fontPath = env->GetStringUTFChars(jfontPath, 0);
        const char *fontFamily = env->GetStringUTFChars(jfontFamily, 0);
        renderer->setDefaultFont(fontPath, fontFamily);
        env->ReleaseStringUTFChars(jfontPath, fontPath);
        env->ReleaseStringUTFChars(jfontFamily, fontFamily);
    }
}

JNIEXPORT void JNICALL EXPORT_RENDERER(addFont) (JNIEnv *env, jobject instance, jstring jname,
                                                 jbyteArray jdata) {
    auto* renderer = getPtr<ASSRenderer>(env, instance, sNativeRendererInstance);
    if (renderer) {
        const char *name = env->GetStringUTFChars(jname, 0);
        char* data = (char*) env->GetPrimitiveArrayCritical(jdata, NULL);
        int length = env->GetArrayLength(jdata);
        renderer->addFont(const_cast<char*>(name), data, length);
        env->ReleasePrimitiveArrayCritical(jdata, data, 0);
        env->ReleaseStringUTFChars(jname, name);
    }
}

JNIEXPORT void JNICALL EXPORT_RENDERER(nativeRelease) (JNIEnv *env, jobject instance) {
    auto* renderer = getPtr<ASSRenderer>(env, instance, sNativeRendererInstance);
    if (renderer) {
        delete renderer;
        setPtr(env, instance, sNativeRendererInstance, NULL);
    }

    // Delete global references to the classes
    if (gClassASSBitmap) {
        env->DeleteGlobalRef(gClassASSBitmap);
        gClassASSBitmap = NULL;
    }
    if (gClassASSFrame) {
        env->DeleteGlobalRef(gClassASSFrame);
        gClassASSFrame = NULL;
    }
}

JNIEXPORT jlong JNICALL EXPORT_RENDERER(nativeCreateTrack) (JNIEnv *env, jobject instance,
                                                            jstring jdata) {
    auto* renderer = getPtr<ASSRenderer>(env, instance, sNativeRendererInstance);
    if (renderer) {
        const char *data = jdata ? env->GetStringUTFChars(jdata, 0) : nullptr;
        int length = jdata ? env->GetStringUTFLength(jdata) : 0;
        ASS_Track* track = renderer->createTrack(data, length);
        if (jdata) {
            env->ReleaseStringUTFChars(jdata, data);
        }
        return (jlong) track;
    } else {
        __android_log_print(ANDROID_LOG_ERROR, sTag,
                            "Cannot create track because renderer is not created");
    }
    return NULL;
}

JNIEXPORT jobject JNICALL EXPORT_RENDERER(nativeGetImage) (JNIEnv *env, jobject instance,
                                                           jlong timeMs, jlong jptr) {
    auto* renderer = getPtr<ASSRenderer>(env, instance, sNativeRendererInstance);
    if (renderer == NULL || jptr <= 0) {
        return NULL;
    }

    int size = 0;
    int changed = 0;
    auto track = reinterpret_cast<ASS_Track *>(jptr);
    ASSBitmap** bitmaps = renderer->getBitmaps(track, (long long) timeMs, &size, &changed);
    if (changed == 0) {
        // No change, leave
        return NULL;
    }

    // Lazy load the frame for return
    if (gClassASSFrame == NULL) {
        const jclass frameClass = env->FindClass(JAVA_PKG_PATH"/ASSFrame");
        gClassASSFrame = reinterpret_cast<jclass>(env->NewGlobalRef(frameClass));
        env->DeleteLocalRef(frameClass);
    }

    // Copy the data over to Java objects and return them
    if (size > 0 && bitmaps) {
        // Lazy create the classes
        if (gClassASSBitmap == NULL) {
            const jclass bitmapClass = env->FindClass(JAVA_PKG_PATH"/ASSBitmap");
            gClassASSBitmap = reinterpret_cast<jclass>(env->NewGlobalRef(bitmapClass));
            env->DeleteLocalRef(bitmapClass);
        }

        jobjectArray jBitmaps = env->NewObjectArray(size, gClassASSBitmap, nullptr);
        jobject jFrame = env->NewObject(gClassASSFrame, sMethodASSFrameCtor, timeMs, jBitmaps);
        for (int i = 0; i < size; ++i) {
            ASSBitmap* b = bitmaps[i];
            jbyteArray data = b->size > 0 ? env->NewByteArray((int) b->size) : nullptr;
            if (data != nullptr) {
                env->SetByteArrayRegion(data, 0, (int) b->size, (const jbyte *) b->buffer);
            }
            jobject jBitmap = env->NewObject(gClassASSBitmap, sMethodASSBitmapCtor, b->x1,
                    b->y1, b->x2, b->y2, data, b->changed, b->stride);
            env->SetObjectArrayElement(jBitmaps, i, jBitmap);
        }
        return jFrame;
    }

    // New frame changed but no data so clear the subtitles
    return env->NewObject(gClassASSFrame, sMethodASSFrameCtor, timeMs, NULL);
}

JNIEXPORT void JNICALL EXPORT_TRACK(nativeAddData) (JNIEnv *env, jobject instance, jlong jptr,
                                                    jstring jdata) {
    auto* track = reinterpret_cast<ASS_Track*>(jptr);
    const char *data = env->GetStringUTFChars(jdata, 0);
    int length = env->GetStringUTFLength(jdata);
    ass_process_data(track, const_cast<char*>(data), length);
    env->ReleaseStringUTFChars(jdata, data);
}

JNIEXPORT void JNICALL EXPORT_TRACK(nativeFlush) (JNIEnv *env, jobject instance, jlong jptr) {
    auto* track = reinterpret_cast<ASS_Track*>(jptr);
    ass_flush_events(track);
}

JNIEXPORT void JNICALL EXPORT_TRACK(nativeRelease) (JNIEnv *env, jobject instance, jlong jptr) {
    auto* track = reinterpret_cast<ASS_Track*>(jptr);
    delete track;
}
}
