#ifndef JNIVIDEORENDERER_H
#define JNIVIDEORENDERER_H

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <mutex>
#include <android/log.h>
#include "IVideoRenderer.h"

class JniVideoRenderer : public IVideoRenderer {
public:
    JniVideoRenderer();
    ~JniVideoRenderer();

    void onSurfaceCreated(JNIEnv* env, jobject surface);
    void onSurfaceDestroyed();

    int renderFrame(AVFrame *frame) override;


private:
    void release();

    std::mutex mMutex;
    ANativeWindow* mWindow;
    AVFrame* mBufferFrame;
    struct SwsContext* mSwsContext;

    double aaa;
};


#endif //JNIVIDEORENDERER_H
