#ifndef BASICCOLORSPACECONVERTER_H
#define BASICCOLORSPACECONVERTER_H

extern "C" {
#include <libavutil/frame.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class BasicYUVConverter {
public:
    BasicYUVConverter();
    virtual ~BasicYUVConverter();

    virtual int convert(AVFrame* src, AVFrame* dst);

protected:
    SwsContext* mSwsContext;
};

#endif //BASICCOLORSPACECONVERTER_H
