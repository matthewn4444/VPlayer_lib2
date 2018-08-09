#ifndef IVIDEORENDERER_H
#define IVIDEORENDERER_H

extern "C" {
#include <libavutil/frame.h>
}

#include <ass/ass.h>

class IVideoRenderer {
public:
virtual bool writeSubtitlesSeparately() = 0;
virtual int writeFrame(AVFrame* videoFrame, AVFrame* subtitleFrame) = 0;
virtual int renderFrame() = 0;
};

#endif //IVIDEORENDERER_H
