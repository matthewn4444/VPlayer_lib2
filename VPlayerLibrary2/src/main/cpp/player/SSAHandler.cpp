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
    int dstStride = vFrame->linesize[0];
    uint8_t* src = subImage->bitmap;
    int srcHeight = subImage->h;
    int srcStride = subImage->stride;

#if __aarch64__
    asm (
    "mov x0, %x[color]\n"
    "mov x1, %x[dst]\n"
    "mov x2, %x[src]\n"
    "mov x3, %x[srcHeight]\n"
    "mov x4, %x[srcStride]\n"
    "mov x5, %x[dstStride]\n"

    // Load 8 slots of 0xFF
    "mov w8, #0xFF\n"
    "dup v2.8b, w8\n"

    // Read color list, load to vectors and inverse alpha
    "ld4r {v3.8b-v6.8b}, [x0]\n"                // Color mask [ARGB]
    "sub v3.8b, v2.8b, v3.8b\n"

    // For loop
    "mov x9, #0\n"
    "yloop:\n"

    "mov x10, #0\n"
    "madd x11, x9, x4, x2\n"                    // Source: src = y * srcStride + src
    "madd x12, x9, x5, x1\n"                    // Dest:   dst = y * dstStride + dst

    "xloop:\n"

    // Load the source and destination data
    "ld1 {v7.8b}, [x11], #8\n"                  // Source pointer
    "ld4 {v8.8b-v11.8b}, [x12]\n"               // Destination pointer [BGRA]

    // Prepare the source alpha
    "and v12.8b, v7.8b, v3.8b\n"                // Source alpha
    "sub v13.8b, v2.8b, v12.8b\n"               // Source alpha inverse

    // Compute red pixel data
    "and v0.8b, v7.8b, v4.8b\n"
    "umull v0.8h, v0.8b, v12.8b\n"              // v = source * alpha
    "umlal v0.8h, v13.8b, v10.8b\n"             // v = v + (bg * alphaInv)
    "uqrshrn v10.8b, v0.8h, #8\n"               // v = v >> 8 or v = v / 256

    // Compute green pixel data
    "and v0.8b, v7.8b, v5.8b\n"
    "umull v0.8h, v0.8b, v12.8b\n"
    "umlal v0.8h, v13.8b, v9.8b\n"
    "uqrshrn v9.8b, v0.8h, #8\n"

    // Compute blue pixel data
    "and v0.8b, v7.8b, v6.8b\n"
    "umull v0.8h, v0.8b, v12.8b\n"
    "umlal v0.8h, v13.8b, v8.8b\n"
    "uqrshrn v8.8b, v0.8h, #8\n"

    // Compute alpha pixel data
    "and v0.8b, v7.8b, v3.8b\n"
    "umull v0.8h, v0.8b, v12.8b\n"
    "umlal v0.8h, v13.8b, v11.8b\n"
    "uqrshrn v11.8b, v0.8h, #8\n"

    // Write back to dst
    "st4 {v8.8b-v11.8b}, [x12], #32\n"

    // End loop, x < srcStride; x += 8
    "add x10, x10, #8\n"
    "cmp x4, x10\n"
    "b.gt xloop\n"

    // End loop, y < scrHeight; y++
    "add x9, x9, #1\n"
    "cmp x3, x9\n"
    "b.gt yloop\n"
    :
    : [color] "r" (&subImage->color), [dst] "r" (dst), [src] "r" (src),
    [srcHeight] "r" (srcHeight), [srcStride] "r" (srcStride) , [dstStride] "r" (dstStride)
    : "x0", "x1", "x2", "x3", "x4", "x5"
    );
#elif __ARM_NEON__
    asm (
    "mov r0, %[color]\n"
    "mov r1, %[dst]\n"
    "mov r2, %[src]\n"
    "mov r3, %[srcHeight]\n"
    "mov r4, %[srcStride]\n"
    "mov r5, %[dstStride]\n"

    // Load 8 slots of 0rFF
    "mov r6, #0xFF\n"
    "vdup.8 d2, r6\n"

    // Read color list, load to vectors and inverse alpha
    "vld4.8 {d3[], d4[], d5[], d6[]}, [r0]\n"
    "vsub.u8 d3, d2, d3\n"

    // For loop
    "mov r6, #0\n"
    "yloop:\n"

    "mov r7, #0\n"
    "mla r8, r6, r4, r2\n"                      // Source: src = y * srcStride + src
    "mla r9, r6, r5, r1\n"                      // Dest:   dst = y * dstStride + dst

    "rloop:\n"

    // Load the source and destination data
    "vld1.8 {d7}, [r8]\n"                       // Source pointer
    "vld4.8 {d8-d11}, [r9]\n"                   // Destination pointer [BGRA]

    // Prepare the source alpha
    "vand.u8 d12, d7, d3\n"                     // Source alpha
    "vsub.u8 d13, d2, d12\n"                    // Source alpha inverse

    // Compute red pixel data
    "vand.u8 d0, d7, d4\n"
    "vmull.u8 q0, d0, d12\n"                    // v = source * alpha
    "vmlal.u8 q0, d13, d10\n"                   // v = v + (bg * alphaInv)
    "vqrshrn.u16 d10, q0, #8\n"                 // v = v >> 8 or v = v / 256

    // Compute green pixel data
    "vand.u8 d0, d7, d5\n"
    "vmull.u8 q0, d0, d12\n"
    "vmlal.u8 q0, d13, d9\n"
    "vqrshrn.u16 d9, q0, #8\n"

    // Compute blue pixel data
    "vand.u8 d0, d7, d6\n"
    "vmull.u8 q0, d0, d12\n"
    "vmlal.u8 q0, d13, d8\n"
    "vqrshrn.u16 d8, q0, #8\n"

    // Compute alpha pixel data
    "vand.u8 d0, d7, d3\n"
    "vmull.u8 q0, d0, d12\n"
    "vmlal.u8 q0, d13, d11\n"
    "vqrshrn.u16 d11, q0, #8\n"

    // Write back to dst
    "vst4.8 {d8-d11}, [r9]\n"

    // End loop, x < srcStride; x += 4
    "add r7, r7, #8\n"
    "add r8, r8, #8\n"
    "add r9, r9, #32\n"
    "cmp r4, r7\n"
    "bgt rloop\n"

    // End loop, y < scrHeight; y++
    "add r6, r6, #1\n"
    "cmp r3, r6\n"
    "bgt yloop\n"
    :
    : [color] "r" (&subImage->color), [dst] "r" (dst), [src] "r" (src),
        [srcHeight] "r" (srcHeight), [srcStride] "r" (srcStride) , [dstStride] "r" (dstStride)
    : "r0", "r1", "r2", "r3", "r4", "r5"
    );
#else
    const uint8_t rgba_color[] = {
            (uint8_t)(subImage->color >> 24),
            (uint8_t) ((subImage->color >> 16) & 0xFF),
            (uint8_t) ((subImage->color >> 8) & 0xFF),
            (uint8_t) ((0xFF - subImage->color) & 0xFF)
    };

    uint8_t rect_r, rect_g, rect_b, rect_a;
    int dest_r, dest_g, dest_b, dest_a;
    int x, y;

    for (y = 0; y < srcHeight; y++) {
        uint32_t* dst2 = (uint32_t *) dst;
        uint8_t* src2 = src;

        for (x = 0; x < subImage->w; x++) {
            uint8_t image_pixel = *(src2++);
            uint32_t *pixel = (dst2++);

            // Expand the subtitle pixel
            rect_r = image_pixel & rgba_color[2];
            rect_g = image_pixel & rgba_color[1];
            rect_b = image_pixel & rgba_color[0];
            rect_a = image_pixel & rgba_color[3];

            dest_a = (pixel[0] >> 24) & 0xff;
            dest_r = (pixel[0] >> 16) & 0xff;
            dest_g = (pixel[0] >> 8) & 0xff;
            dest_b = pixel[0] & 0xff;

            // Pixel blending
	        dest_a = (((dest_a * (0xff - rect_a)) + (rect_a * rect_a))/0xff);
	        dest_r = (((dest_r * (0xff - rect_a)) + (rect_r * rect_a))/0xff);
            dest_g = (((dest_g * (0xff - rect_a)) + (rect_g * rect_a))/0xff);
            dest_b = (((dest_b * (0xff - rect_a)) + (rect_b * rect_a))/0xff);

            // Write pixel back to frame
            pixel[0] = (uint32_t) ((dest_a << 24) | (dest_r << 16) | (dest_g << 8) | dest_b);
        }
        dst += dstStride;
        src += srcStride;
    }
#endif
}
