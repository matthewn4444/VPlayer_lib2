#ifndef COLORSPACE16BITCONVERTER_H
#define COLORSPACE16BITCONVERTER_H

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
};

#include "BasicYUVConverter.h"

class YUV16to8Converter : public BasicYUVConverter {
public:
    YUV16to8Converter(AVCodecContext *context, enum AVPixelFormat midFormat);
    virtual ~YUV16to8Converter();

    virtual int convert(AVFrame *src, AVFrame *dst) override;

protected:
    virtual bool reduceYUV16to8bit(AVFrame *srcFrame, AVFrame *dstFrame);
    void reduce16BitChannelDepth(uint16_t *src, uint8_t *dst, int srcStride, int dstStride,
                                 int width, int height, bool isBigEndian, int scaleFactor);

    int mFrameBitDepth;
    bool mFrameBitsBigEndian;
    AVFrame* mTmpFrame;
};

#endif //COLORSPACE16BITCONVERTER_H
