#ifndef JNIVIDEORENDERER_H
#define JNIVIDEORENDERER_H

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
}
#include <android/native_window_jni.h>
#include <mutex>
#include <android/log.h>
#include "IVideoRenderer.h"

class JniVideoRenderer : public IVideoRenderer {
public:
    JniVideoRenderer();
    ~JniVideoRenderer();

    void onSurfaceCreated(JNIEnv* env, jobject vSurface, jobject sSurface);
    void onSurfaceDestroyed();

    int writeFrame(AVFrame* videoFrame, AVFrame* subtitleFrame) override;
    int renderFrame() override;

    bool writeSubtitlesSeparately() override;

private:
    int writeFrameToWindow(AVFrame* frame, ANativeWindow* window);
    void release();

    std::mutex mMutex;
    ANativeWindow* mWindow;
    ANativeWindow* mSubWindow;
    bool mSubWindowHasData;
};

#endif //JNIVIDEORENDERER_H
