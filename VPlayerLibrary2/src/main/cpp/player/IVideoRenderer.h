#ifndef IVIDEORENDERER_H
#define IVIDEORENDERER_H

extern "C" {
#include <libavutil/frame.h>
}

class IVideoRenderer {
public:
virtual int renderFrame(AVFrame* frame) = 0;        // TODO pass the frame of subtitles
};

#endif //IVIDEORENDERER_H
