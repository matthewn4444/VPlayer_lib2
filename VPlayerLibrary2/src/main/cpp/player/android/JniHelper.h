#ifndef VPLAYER_LIB2_JNIHANDLER_H
#define VPLAYER_LIB2_JNIHANDLER_H

#include <mutex>
#include <thread>
#include <map>
#include <android/log.h>
#include <jni.h>

#define JAVA_PKG_PATH "com/matthewn4444/vplayerlibrary2"
#define JAVA_EXPORT_NAME2(name,package) JNICALL Java_##package##_##name
#define JAVA_EXPORT_NAME1(name,package) JAVA_EXPORT_NAME2(name,package)

typedef struct {
    const char* name;
    const char* signature;
} JavaField, JavaMethod;

template <class Ptr>
Ptr* getPtr(JNIEnv *env, jobject thiz, jfieldID field) {
    return (Ptr*) env->GetLongField(thiz, field);
}

template<class Ptr>
void setPtr(JNIEnv *env, jobject instance, jfieldID field, Ptr ptr) {
    env->SetLongField(instance, field, (jlong) ptr);
}

jfieldID getJavaField(JNIEnv *env, jclass clazz, JavaField field);
jfieldID getStaticJavaField(JNIEnv *env, jclass clazz, JavaField field);
jmethodID getJavaMethod(JNIEnv *env, jclass clazz, JavaMethod method);
jmethodID getStaticJavaMethod(JNIEnv *env, jclass clazz, JavaMethod method);

class JniHelper {
public:
    JniHelper(JNIEnv* env, jobject instance);
    virtual ~JniHelper();

    virtual jobject makeInstanceGlobalRef(JNIEnv* env);
    virtual void deleteInstanceGlobalRef(JNIEnv* env);

    JNIEnv* attachToThisThread();
    void detachFromThisThread();

    JNIEnv* getEnv();

protected:
    static JavaVM* sJavaVM;

    jobject mInstance;
    std::mutex mMutex;

private:
    bool mHasGlobalRef;
    std::map<std::thread::id, JNIEnv*> mEnvLookup;
};

#endif // VPLAYER_LIB2_JNIHANDLER_H