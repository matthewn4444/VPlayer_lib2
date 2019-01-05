#include "JniHelper.h"

static const char* sTag = "JniHelper";

JavaVM* JniHelper::sJavaVM = NULL;

jfieldID getJavaField(JNIEnv* env, jclass clazz, JavaField field) {
    return env->GetFieldID(clazz, field.name, field.signature);
}

jfieldID getStaticJavaField(JNIEnv* env, jclass clazz, JavaField field) {
    return env->GetStaticFieldID(clazz, field.name, field.signature);
}

jmethodID getJavaMethod(JNIEnv *env, jclass clazz, JavaMethod method) {
    return env->GetMethodID(clazz, method.name, method.signature);
}

jmethodID getStaticJavaMethod(JNIEnv *env, jclass clazz, JavaMethod method) {
    return env->GetStaticMethodID(clazz, method.name, method.signature);
}

JniHelper::JniHelper(JNIEnv *env, jobject instance) :
        mInstance(instance),
        mHasGlobalRef(false) {
    if (sJavaVM == NULL && env->GetJavaVM(&sJavaVM) < 0) {
        __android_log_print(ANDROID_LOG_FATAL, sTag, "Failed to get JNI Java VM for init");
    }
}

JniHelper::~JniHelper() {
}

jobject JniHelper::makeInstanceGlobalRef(JNIEnv *env) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (env && !mHasGlobalRef) {
        mInstance = env->NewGlobalRef(mInstance);
        mHasGlobalRef = true;
        return mInstance;
    }
    return NULL;
}

void JniHelper::deleteInstanceGlobalRef(JNIEnv *env) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (mHasGlobalRef) {
        if (env) {
            env->DeleteGlobalRef(mInstance);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, sTag,
                                "Cannot delete global reference to instance!");
        }
        mHasGlobalRef = false;
        mInstance = NULL;
    }
}

JNIEnv *JniHelper::attachToThisThread() {
    JNIEnv* env = getEnv();
    if (env) {
        return env;
    }
    std::lock_guard<std::mutex> lk(mMutex);
    std::thread::id id = std::this_thread::get_id();
    switch (sJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6)) {
        case JNI_OK:
            break;
        case JNI_EDETACHED:
            if (sJavaVM->AttachCurrentThread(&env, NULL) != 0) {
                __android_log_print(ANDROID_LOG_ERROR, sTag, "JNI cannot attach to thread");
            }
            mEnvLookup[id] = env;
            return env;
        case JNI_EVERSION:
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Bad java version");
            break;
        default:
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Unknown ");
            break;
    }
    return NULL;
}

void JniHelper::detachFromThisThread() {
    if (getEnv()) {
        std::lock_guard<std::mutex> lk(mMutex);
        mEnvLookup.erase(std::this_thread::get_id());
        sJavaVM->DetachCurrentThread();
    }
}

JNIEnv *JniHelper::getEnv() {
    if (sJavaVM == NULL) {
        return NULL;
    }
    std::lock_guard<std::mutex> lk(mMutex);
    std::thread::id id = std::this_thread::get_id();
    std::map<std::thread::id, JNIEnv*>::iterator it = mEnvLookup.find(id);
    if (it != mEnvLookup.end()) {
        return it->second;
    }
    return NULL;
}
