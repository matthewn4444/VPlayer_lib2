#include "ImageSubHandler.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

#define SUBPICTURE_QUEUE_SIZE 16

static const char* sTag = "ImageSubHandler";
static const double CACHE_FRAME_TIMEOUT_SEC = 20.0;

ImageSubHandler::ImageSubHandler(AVCodecID codecID) :
        SubtitleHandlerBase(codecID),
        mSwsContext(NULL),
        mQueue(NULL),
        mCodecWidth(0),
        mInvalidate(false) {
}

ImageSubHandler::~ImageSubHandler() {
    if (mQueue) {
        delete mQueue;
        mQueue = NULL;
    }
    for (int i = 0; i < mFrameCache.size(); ++i) {
        av_freep(mFrameCache[i].frame);
    }
    if (mSwsContext) {
        sws_freeContext(mSwsContext);
        mSwsContext = NULL;
    }
}

int ImageSubHandler::open(AVCodecContext *cContext, AVFormatContext *fContext) {
    mCodecWidth = cContext->width;
    mInvalidate = true;

    if (mQueue) {
        delete mQueue;
    }
    mQueue = new FrameQueue(false /* isAVQueue */, SUBPICTURE_QUEUE_SIZE);
    return 0;
}

int ImageSubHandler::blendToFrame(double pts, AVFrame *vFrame, intptr_t pktSerial, bool force) {
    int ret = 0;
    while (mQueue->getNumRemaining() > 0) {
        Frame* sp = mQueue->peekFirst(), *sp2 = NULL;
        if (!sp->subtitle()) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Frame is not subtitle frame");
            return AVERROR_INVALIDDATA;
        }
        if (mQueue->getNumRemaining() > 1 && !(sp2 = mQueue->peekNext())->subtitle()) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Next frame is not subtitle frame");
            return AVERROR_INVALIDDATA;
        }

        if (sp->serial() != pktSerial
                || (sp2 && pts > sp2->startPts())
                || (!sp2 && pts > sp->endPts())) {
            // New subtitle change
            mQueue->pushNext();
            mInvalidate = true;
        } else {
            if (pts >= sp->startPts() && (mInvalidate || force)) {
                AVSubtitle* sub = sp->subtitle();
                if (sub->format == 0 /* graphics */) {
                    if (sub->num_rects == 0 && mInvalidate) {
                        // No subtitles shown
                        mInvalidate = false;
                        ret = 1;
                    }

                    // Blend each subtitle rect to the frame
                    for (int i = 0; i < sub->num_rects; i++) {
                        // Add a new frame cache that is needed to hold subtitles
                        if (mFrameCache.size() <= i) {
                            AVFrame *f = av_frame_alloc();
                            if (!f) {
                                __android_log_print(ANDROID_LOG_ERROR, sTag,
                                                    "Unable to allocate temporary avframe");
                                return AVERROR(ENOMEM);
                            }
                            mFrameCache.push_back({0, 0, f, 0});
                        }
                        FrameCache& cache = mFrameCache[i];
                        AVFrame *tmpFrame = cache.frame;
                        cache.lastUsed = pts;

                        // Subtitle needs to be reconverted and resized to cached image
                        if (mInvalidate) {
                            if ((ret = prepareSubFrame(sub->rects[i], cache, vFrame->width,
                                                       vFrame->height)) < 0) {
                                return ret;
                            }

                            // Delete any frames unused except the first after the cache timeout
                            for (long j = mFrameCache.size() - 1; j >= 1; --j) {
                                if (pts - mFrameCache[j].lastUsed > CACHE_FRAME_TIMEOUT_SEC) {
                                    av_freep(mFrameCache[j].frame);
                                    mFrameCache.pop_back();
                                } else {
                                    break;
                                }
                            }
                            ret = 1;
                        }
                        blendFrames(vFrame, tmpFrame, cache.x, cache.y);
                        if (force) {
                            // Re-render this frame if forced
                            ret = 1;
                        }
                    }
                    mInvalidate = false;
                } else {
                    __android_log_print(ANDROID_LOG_WARN, sTag,
                                        "Cannot render subtitle with type %d", sub->format);
                }
            }
            break;
        }
    }
    return ret;
}

void ImageSubHandler::setDefaultFont(const char *fontPath, const char *fontFamily) {
    // Not used
}

bool ImageSubHandler::handleDecodedSubtitle(AVSubtitle *subtitle, intptr_t pktSerial) {
    Frame* sp;
    if (!(sp = mQueue->peekWritable())) {
        return false;
    }
    bool ret = subtitle->format == 0; // image subs
    if (ret) {
        sp->updateAsSubtitle(0, 0, pktSerial);
        mQueue->push();
    }
    return ret;
}

AVSubtitle *ImageSubHandler::getSubtitle() {
    Frame* sp = mQueue->peekWritable();
    return sp ? sp->subtitle() : NULL;
}

void ImageSubHandler::blendFrames(AVFrame *dstFrame, AVFrame *srcFrame, int srcX, int srcY) {
    uint8_t *dst = dstFrame->data[0] + dstFrame->linesize[0] * srcY + srcX * 4;
    int dstStride = dstFrame->linesize[0];
    uint8_t *src = srcFrame->data[0];
    int srcStride = srcFrame->linesize[0];

    int rect_r, rect_g, rect_b, rect_a;
    int dest_r, dest_g, dest_b, dest_a;
    int x2, y2;

    for (y2 = 0; y2 < srcFrame->height; y2++) {
        uint32_t *dst2 = (uint32_t *) dst;
        uint32_t *src2 = (uint32_t *) src;

        for (x2 = 0; x2 < srcFrame->width; x2++) {
            uint32_t *image_pixel = (src2++);
            uint32_t *pixel = (dst2++);

            // Expand the subtitle pixel
            rect_a = (image_pixel[0] >> 24) & 0xff;
            rect_r = (image_pixel[0] >> 16) & 0xff;
            rect_g = (image_pixel[0] >> 8) & 0xff;
            rect_b = image_pixel[0] & 0xff;

            dest_a = (pixel[0] >> 24) & 0xff;
            dest_r = (pixel[0] >> 16) & 0xff;
            dest_g = (pixel[0] >> 8) & 0xff;
            dest_b = pixel[0] & 0xff;

            // Pixel blending
            dest_a = (((dest_a * (0xff - rect_a)) + (rect_a * rect_a)) / 0xff);
            dest_r = (((dest_r * (0xff - rect_a)) + (rect_r * rect_a)) / 0xff);
            dest_g = (((dest_g * (0xff - rect_a)) + (rect_g * rect_a)) / 0xff);
            dest_b = (((dest_b * (0xff - rect_a)) + (rect_b * rect_a)) / 0xff);

            // Write pixel back to frame
            pixel[0] = (uint32_t) ((dest_a << 24) | (dest_r << 16) | (dest_g << 8) | dest_b);
        }
        dst += dstStride;
        src += srcStride;
    }
}

bool ImageSubHandler::areFramesPending() {
    return mQueue != NULL && mQueue->getNumRemaining() > 0;
}

void ImageSubHandler::invalidateFrame() {
    mInvalidate = true;
}

void ImageSubHandler::flush() {
    // Not used
}

int ImageSubHandler::prepareSubFrame(AVSubtitleRect* rect, FrameCache& cache, int vWidth,
                                     int vHeight) {
    int ret = 0;
    AVFrame* tmpFrame = cache.frame;

    // Calculate the bounds of the resized subtitle image
    const float ratio = (float) vWidth / mCodecWidth;
    cache.x = av_clip((int) roundf(rect->x * ratio), 0, vWidth);
    cache.y = av_clip((int) roundf(rect->y * ratio), 0, vHeight);
    int w = av_clip((int) roundf(rect->w * ratio), 0, vWidth - cache.x);
    int h = av_clip((int) roundf(rect->h * ratio), 0, vHeight - cache.y);

    // Resize the cached frame if too small
    if ((w > tmpFrame->width || h > tmpFrame->height)) {
        if ((ret = av_image_alloc(tmpFrame->data, tmpFrame->linesize, w, h,
                                  AV_PIX_FMT_BGRA, 1)) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate image");
            return ret;
        }
    }

    // Resize and convert the color space of the subtitle
    if (!(mSwsContext = sws_getCachedContext(mSwsContext, rect->w, rect->h, AV_PIX_FMT_PAL8, w, h,
                                             AV_PIX_FMT_BGRA, 0, NULL, NULL, NULL))) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot initialize the conversion context");
        return AVERROR(EINVAL);
    }
    if ((ret = sws_scale(mSwsContext, (const uint8_t *const *) rect->data, rect->linesize, 0,
                         rect->h, tmpFrame->data, tmpFrame->linesize)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot scale subtitle frame to video");
        return ret;
    }
    tmpFrame->width = w;
    tmpFrame->height = h;
    return ret;
}
