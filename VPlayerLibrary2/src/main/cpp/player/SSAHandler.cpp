#include "SSAHandler.h"
#include "ASSBitmap.h"

static const char* sTag = "SSAHandler";
static const char *sFontMimeTypes[] = {
        "application/x-font-ttf",
        "application/x-truetype-font",
        "application/vnd.ms-opentype",
        "application/x-font"
};

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
        mRenderer(NULL),
        mAssTrack(NULL),
        mTmpSubtitle({0}),
        mLastPts(0) {
}

SSAHandler::~SSAHandler() {
    if (mAssTrack) {
        ass_free_track(mAssTrack);
        mAssTrack = NULL;
    }
    if (mRenderer) {
        delete mRenderer;
        mRenderer = NULL;
    }
    avsubtitle_free(&mTmpSubtitle);
}

int SSAHandler::open(AVCodecContext *cContext, AVFormatContext *fContext) {
    if (mRenderer) {
        delete mRenderer;
    }

    mRenderer = new ASSRenderer();
    if (mRenderer->getError() < 0) {
        return mRenderer->getError();
    }

    // Create the track
    const char* header = cContext->subtitle_header ? (char*) cContext->subtitle_header : NULL;
    const int headerSize = cContext->subtitle_header ? cContext->subtitle_header_size : 0;
    mAssTrack = mRenderer->createTrack(header, headerSize);
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
                mRenderer->addFont(tag->value, (char*) st->codecpar->extradata,
                                   st->codecpar->extradata_size);
            } else {
                __android_log_print(ANDROID_LOG_WARN, sTag,
                                    "Ignoring font attachment, no filename.");
            }
        }
    }
    return 0;
}

void SSAHandler::abort() {
    // Is not needed
}

int SSAHandler::blendToFrame(double pts, AVFrame *vFrame, intptr_t pktSerial, bool force) {
    ASS_Image* image;
    int changed = 0;
    mRenderer->setSize(vFrame->width, vFrame->height);
    {
        std::lock_guard<std::mutex> lk(mAssMutex);
        image = mRenderer->renderFrame(mAssTrack, (int64_t) vFrame->pts, &changed);
        if (force) {
            changed = 2;
        }
    }
    if (changed == 2) {
        for (; image != NULL; image = image->next) {
            uint8_t* dst = vFrame->data[0] + vFrame->linesize[0] * image->dst_y + image->dst_x * 4;
            ASSBitmap::blendSubtitle(dst, (size_t) vFrame->linesize[0], image);
        }
        mLastPts = vFrame->pts;
    }
    return changed == 2 ? 2 : 0;
}

void SSAHandler::setDefaultFont(const char *fontPath, const char *fontFamily) {
    if (mRenderer && fontPath && fontFamily) {
        mRenderer->setDefaultFont(fontPath, fontFamily);
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

void SSAHandler::flush() {
    ass_flush_events(mAssTrack);
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
