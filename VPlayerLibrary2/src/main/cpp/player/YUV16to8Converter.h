#ifndef COLORSPACE16BITCONVERTER_H
#define COLORSPACE16BITCONVERTER_H

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
};

#include "BasicYUVConverter.h"
#include "convert.h"

class YUV16to8Converter : public BasicYUVConverter {
public:
    YUV16to8Converter(AVCodecContext *context, enum AVPixelFormat midFormat);
    virtual ~YUV16to8Converter();

    virtual int convert(AVFrame *src, AVFrame *dst) override;

protected:
    virtual bool reduceYUV16to8bit(AVFrame *srcFrame, AVFrame *dstFrame);
    void reduce16BitChannelDepth(const uint16_t *src, uint8_t *dst, size_t srcStride,
                                 size_t dstStride, size_t width, size_t height);

    int mFrameBitDepth;
    bool mFrameBitsBigEndian;
    AVFrame* mTmpFrame;
#if CONVERT_16_TO_8_ASM_ENABLED
    bitconv::conv16_8_func mConversionFn;
#endif
};

#endif //COLORSPACE16BITCONVERTER_H
