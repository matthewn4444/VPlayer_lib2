#include "SSAHandler.h"

#if __aarch64__
#include <arm_neon.h>
#endif

static const char* sTag = "SSAHandler";
static const char *sFontMimeTypes[] = {
        "application/x-font-ttf",
        "application/x-truetype-font",
        "application/vnd.ms-opentype",
        "application/x-font"
};

static void ass_log(int ass_level, const char *fmt, va_list args, void *ctx) {
  //    _log(fmt, args);
}

static int isFontAttachment(const AVStream * st) {
    const AVDictionaryEntry *tag = av_dict_get(st->metadata, "mimetype", NULL, AV_DICT_MATCH_CASE);
    if (tag) {
        for (int n = 0; sFontMimeTypes[n]; n++) {
            if (strcmp(sFontMimeTypes[n], tag->value) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

SSAHandler::SSAHandler(AVCodecID codecID) :
        SubtitleHandlerBase(codecID),
        mAssLibrary(NULL),
        mAssRenderer(NULL),
        mAssTrack(NULL),
        mTmpSubtitle({0}) {
}

SSAHandler::~SSAHandler() {
    if (mAssTrack) {
        ass_free_track(mAssTrack);
        mAssTrack = NULL;
    }
    if (mAssRenderer) {
        ass_renderer_done(mAssRenderer);
        mAssRenderer = NULL;
    }
    if (mAssLibrary) {
        ass_library_done(mAssLibrary);
        mAssLibrary = NULL;
    }
    avsubtitle_free(&mTmpSubtitle);
}

int SSAHandler::open(AVCodecContext *cContext, AVFormatContext *fContext) {
    mAssLibrary = ass_library_init();
    if (!mAssLibrary) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate ass library");
        return AVERROR(ENOMEM);
    }
    ass_set_message_cb(mAssLibrary, ass_log, NULL);

    mAssRenderer = ass_renderer_init(mAssLibrary);
    if (!mAssRenderer) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate ass renderer");
        return AVERROR(ENOMEM);
    }
    mAssTrack = ass_new_track(mAssLibrary);
    if (!mAssTrack) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate ass track");
        return AVERROR(ENOMEM);
    }

    // Load font attachments from video into the engine
    for (int i = 0; i < fContext->nb_streams; i++) {
        const AVStream *st = fContext->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT && isFontAttachment(st)) {
            const AVDictionaryEntry *tag = av_dict_get(st->metadata, "filename", NULL,
                                                       AV_DICT_MATCH_CASE);
            if (tag) {
                __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Loading font: %s", tag->value);
                ass_add_font(mAssLibrary, tag->value,
                             (char*) st->codecpar->extradata,
                             st->codecpar->extradata_size);
            } else {
                __android_log_print(ANDROID_LOG_WARN, sTag,
                                    "Ignoring font attachment, no filename.");
            }
        }
    }
    ass_set_extract_fonts(mAssLibrary, true);
    ass_set_fonts( mAssRenderer, NULL, NULL, ASS_FONTPROVIDER_AUTODETECT, NULL, 1 );

    // Set the subtitle header
    if (cContext->subtitle_header) {
        ass_process_codec_private(mAssTrack, (char *) cContext->subtitle_header,
                                  cContext->subtitle_header_size);
    }
    return 0;
}

int SSAHandler::blendToFrame(double pts, AVFrame *vFrame, intptr_t pktSerial, bool force) {
    ASS_Image* image;
    int changed = 0;
    ass_set_frame_size(mAssRenderer, vFrame->width, vFrame->height);
    {
        std::lock_guard<std::mutex> lk(mAssMutex);
        image = ass_render_frame(mAssRenderer, mAssTrack, (long long int) vFrame->pts, &changed);
    }
    if (changed == 2 || force) {
        for (; image != NULL; image = image->next) {
            blendSSA(vFrame, image);
        }
    }
    return changed == 2 ? 2 : 0;
}

void SSAHandler::setDefaultFont(const char *fontPath, const char *fontFamily) {
    if (mAssRenderer && fontPath && fontFamily) {
        ass_set_fonts(mAssRenderer, fontPath, fontFamily, ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
    }
}

AVSubtitle *SSAHandler::getSubtitle() {
    return &mTmpSubtitle;
}

bool SSAHandler::areFramesPending() {
    // Does not process frames in libass
    return false;
}

void SSAHandler::invalidateFrame() {
    // Is not used
}

bool SSAHandler::handleDecodedSubtitle(AVSubtitle* subtitle, intptr_t pktSerial) {
    bool ret = subtitle->format == 1; // text subs
    if (ret) {
        std::lock_guard<std::mutex> lk(mAssMutex);
        for (int i = 0; i < subtitle->num_rects; i++) {
            char* text = subtitle->rects[i]->ass;
            ass_process_data(mAssTrack, text, (int) strlen(text));
        }
    }
    avsubtitle_free(subtitle);
    return ret;
}

void SSAHandler::blendSSA(AVFrame *vFrame, const ASS_Image *subImage) {
    uint8_t* dst = vFrame->data[0] + vFrame->linesize[0] * subImage->dst_y + subImage->dst_x * 4;
    const uint8_t* src = subImage->bitmap;
    const size_t width = (size_t) subImage->w;
    const size_t height = (size_t) subImage->h;

    // Calculate the distance between the end of one line to the start of the next
    // libass stride is per pixel while AVFrame stride is by bytes
    const size_t srcStrideOffset = (size_t) (subImage->stride - ceil(width / 8.0) * 8);
    const size_t dstStrideOffset = (size_t) (vFrame->linesize[0] - (ceil(width / 8.0) * 8) * 4);

#if __aarch64__
    asm volatile (
        // Load 8 slots of 0xFF
        "mov            w8, #0xFF                           \n"
        "dup            v0.8b, w8                           \n"

        // Read color list [ARGB], load to vectors and inverse alpha
        "ld4r           { v2.8b - v5.8b }, [%0]             \n"
        "sub            v2.8b, v0.8b, v2.8b                 \n"

        // For loop, count down the width
        "1:                                                 \n"
        "mov            x0, %3                              \n"

        // Load the source and destination data [BGRA]
        "2:                                                 \n"
        "ld1            { v6.8b }, [%1], #8                 \n"
        "ld4            { v7.8b - v10.8b }, [%2]            \n"

        // Prepare the source alpha
        "and            v11.8b, v6.8b, v2.8b                \n"     // Source alpha
        "sub            v12.8b, v0.8b, v11.8b               \n"     // Source alpha inverse

        // Compute red pixel data
        "and            v1.8b, v6.8b, v3.8b                 \n"
        "umull          v1.8h, v1.8b, v11.8b                \n"     // v = source * alpha
        "umlal          v1.8h, v12.8b, v9.8b                \n"     // v = v + (bg * alphaInv)
        "uqrshrn        v9.8b, v1.8h, #8                    \n"     // v = v >> 8 or v = v / 256

        // Compute green pixel data
        "and            v1.8b, v6.8b, v4.8b                 \n"
        "umull          v1.8h, v1.8b, v11.8b                \n"
        "umlal          v1.8h, v12.8b, v8.8b                \n"
        "uqrshrn        v8.8b, v1.8h, #8                    \n"

        // Compute blue pixel data
        "and            v1.8b, v6.8b, v5.8b                 \n"
        "umull          v1.8h, v1.8b, v11.8b                \n"
        "umlal          v1.8h, v12.8b, v7.8b                \n"
        "uqrshrn        v7.8b, v1.8h, #8                    \n"

        // Compute alpha pixel data
        "and            v1.8b, v6.8b, v2.8b                 \n"
        "umull          v1.8h, v1.8b, v11.8b                \n"
        "umlal          v1.8h, v12.8b, v10.8b               \n"
        "uqrshrn        v10.8b, v1.8h, #8                   \n"

        // Write back to dst
        "st4            { v7.8b - v10.8b }, [%2], #32       \n"

        // End loop, x = width; x > 0; x -= 8
        "subs           x0, x0, #8                          \n"
        "b.gt           2b                                  \n"

        // End loop, y = height; y > 0; y--
        // Add the offset to src/dst to get the next pixel
        "add            %1, %1, %5                          \n"
        "add            %2, %2, %6                          \n"
        "subs           %4, %4, #1                          \n"
        "b.gt           1b                                  \n"
    :
    :   "r" (&subImage->color),
        "r" (src),
        "r" (dst),
        "r" (width),
        "r" (height),
        "r" (srcStrideOffset) ,
        "r" (dstStrideOffset)
    :   "memory", "cc", "x0",
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12"
    );
#elif __ARM_NEON__
    asm volatile (
        // Load 8 slots of 0rFF
        "mov            r0, #0xFF                           \n"
        "vdup.8         d2, r0                              \n"

        // Read color list [ARGB], load to vectors and inverse alpha
        "vld4.8         { d3[] - d6[] }, [%0]               \n"
        "vsub.u8        d3, d2, d3                          \n"

        // For loop, count down the width
        "1:                                                 \n"

        "mov            r0, %3                              \n"

        // Load the source and destination data
        "2:                                                 \n"
        "vld1.8         { d7 }, [%1]!                       \n"
        "vld4.8         { d8 - d11 }, [%2]                  \n"

        // Prepare the source alpha
        "vand.u8        d12, d7, d3                         \n"     // Source alpha
        "vsub.u8        d13, d2, d12                        \n"     // Source alpha inverse

        // Compute red pixel data
        "vand.u8        d1, d7, d4                          \n"
        "vmull.u8       q0, d1, d12                         \n"     // v = source * alpha
        "vmlal.u8       q0, d13, d10                        \n"     // v = v + (bg * alphaInv)
        "vqrshrn.u16    d10, q0, #8                         \n"     // v = v >> 8 or v = v / 256

        // Compute green pixel data
        "vand.u8        d1, d7, d5                          \n"
        "vmull.u8       q0, d1, d12                         \n"
        "vmlal.u8       q0, d13, d9                         \n"
        "vqrshrn.u16    d9, q0, #8                          \n"

        // Compute blue pixel data
        "vand.u8        d1, d7, d6                          \n"
        "vmull.u8       q0, d1, d12                         \n"
        "vmlal.u8       q0, d13, d8                         \n"
        "vqrshrn.u16    d8, q0, #8                          \n"

        // Compute alpha pixel data
        "vand.u8        d1, d7, d3                          \n"
        "vmull.u8       q0, d1, d12                         \n"
        "vmlal.u8       q0, d13, d11                        \n"
        "vqrshrn.u16    d11, q0, #8                         \n"

        // Write back to dst
        "vst4.8         { d8 - d11 }, [%2]!                 \n"

        // End loop, x = width; x > 0; x -= 8
        "subs           r0, r0, #8                          \n"
        "bgt            2b                                  \n"

        // End loop, y = height; y > 0; y--
        // Add the offset to src/dst to get the next pixel
        "add            %1, %1, %5                          \n"
        "add            %2, %2, %6                          \n"
        "subs           %4, %4, #1                          \n"
        "bgt            1b                                  \n"
    :
    :   "r" (&subImage->color),
        "r" (src),
        "r" (dst),
        "r" (width),
        "r" (height),
        "r" (srcStrideOffset),
        "r" (dstStrideOffset)
    :   "memory", "cc", "r0",
        "d0", "d1", "d2", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11", "d12", "d13"
    );
#else
    const uint8_t rgba_color[] = {
            (uint8_t)(subImage->color >> 24),
            (uint8_t) ((subImage->color >> 16) & 0xFF),
            (uint8_t) ((subImage->color >> 8) & 0xFF),
            (uint8_t) ((0xFF - subImage->color) & 0xFF)
    };

    uint8_t srcR, srcG, srcB, srcA;
    size_t dstR, dstG, dstB, dstA;
    size_t x, y;

    for (y = height; y > 0; --y) {
        const uint8_t* _src = src;
        uint32_t* _dst = (uint32_t *) dst;

        for (x = width; x > 0; --x) {
            uint8_t srcPixel = *(_src++);
            uint32_t *dstPixel = _dst++;

            // Expand the subtitle pixel
            srcR = srcPixel & rgba_color[2];
            srcG = srcPixel & rgba_color[1];
            srcB = srcPixel & rgba_color[0];
            srcA = srcPixel & rgba_color[3];

            dstA = (dstPixel[0] >> 24) & 0xff;
            dstR = (dstPixel[0] >> 16) & 0xff;
            dstG = (dstPixel[0] >> 8) & 0xff;
            dstB = dstPixel[0] & 0xff;

            // Pixel blending
	        dstA = (((dstA * (0xff - srcA)) + (srcA * srcA))/0xff);
	        dstR = (((dstR * (0xff - srcA)) + (srcR * srcA))/0xff);
            dstG = (((dstG * (0xff - srcA)) + (srcG * srcA))/0xff);
            dstB = (((dstB * (0xff - srcA)) + (srcB * srcA))/0xff);

            // Write pixel back to frame
            dstPixel[0] = (uint32_t) ((dstA << 24) | (dstR << 16) | (dstG << 8) | dstB);
        }
        src += subImage->stride;
        dst += vFrame->linesize[0];
    }
#endif
}
