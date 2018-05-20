#ifndef IVIDEORENDERER_H
#define IVIDEORENDERER_H

extern "C" {
#include <libavutil/frame.h>
}

#include <ass/ass.h>

class IVideoRenderer {
public:
virtual int writeFrame(AVFrame *frame) = 0;
virtual int renderFrame() = 0;
};

#endif //IVIDEORENDERER_H
