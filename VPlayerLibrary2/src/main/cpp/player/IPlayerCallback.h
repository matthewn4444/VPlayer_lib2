#ifndef PLAYERCALLBACK_H
#define PLAYERCALLBACK_H

#include <libavutil/dict.h>
#include "IAudioRenderer.h"

class IPlayerCallback {
public:

    class UniqueCallback {
    public:
        UniqueCallback(IPlayerCallback *callback) :
                mCallback(callback) {
            if (mCallback && mCallback->onThreadStart()) {
                mThreadStarted = true;
            }
        }

        ~UniqueCallback() {
            if (mThreadStarted) {
                mCallback->onThreadEnd();
            }
        }

        IPlayerCallback *callback() {
            return mCallback;
        }

    private:
        IPlayerCallback *mCallback;
        bool mThreadStarted;
    };

    virtual void onError(int, const char *, const char *) = 0;

    virtual void onMetadataReady(AVDictionary *, AVDictionary **,
                                 size_t, AVDictionary **, size_t, AVDictionary **, size_t) = 0;

    virtual void onStreamReady() = 0;

    virtual void onStreamFinished() = 0;

    virtual IAudioRenderer* createAudioRenderer(AVCodecContext *context) = 0;

    virtual bool onThreadStart() = 0;

    virtual void onThreadEnd() = 0;
};

#endif //PLAYERCALLBACK_H
