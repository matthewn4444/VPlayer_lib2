#include "VideoStream.h"

// no AV sync correction is done if below the minimum AV sync threshold
#define AV_SYNC_THRESHOLD_MIN 0.04
// AV sync correction is done if above the maximum AV sync threshold
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1

#define AV_SYNC_THRESHOLD(X) FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, X))

#define REFRESH_RATE 0.01
#define BUFFER_STRING_LENGTH 64
#define _log(...) __android_log_print(ANDROID_LOG_INFO, "VideoStream", __VA_ARGS__);

static const char* sTag = "VideoStream";

VideoStream::VideoStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback) :
        AVComponentStream(context, AVMEDIA_TYPE_VIDEO, flushPkt, callback, VIDEO_PIC_QUEUE_SIZE),
        mFrameStepMode(false),
        mAllowDropFrames(true),
        mVideoRenderer(NULL),
        mForceRefresh(false),
        mFrameTimer(0),
        mSubStream(NULL),
        mEarlyFrameDrops(0),
        mMaxFrameDuration(0),
        mLateFrameDrops(0) {
}

VideoStream::~VideoStream() {
}

void VideoStream::setPaused(bool paused, int pausePlayRet) {
    if (!paused) {
        mFrameTimer = mClock->getTimeSinceLastUpdate();
        if (pausePlayRet != AVERROR(ENOSYS)) {
            mClock->paused = false;
        }
        mClock->updatePts();
    }
    AVComponentStream::setPaused(paused, pausePlayRet);
}


void VideoStream::setVideoRenderer(IVideoRenderer *videoRenderer) {
    mVideoRenderer = videoRenderer;
    spawnRendererThreadIfHaveNot();
}

bool VideoStream::allowFrameDrops() {
    return mAllowDropFrames && getClock() != getMasterClock();
}

int VideoStream::open() {
    int ret = AVComponentStream::open();
    mForceRefresh = false;
    mMaxFrameDuration = (mFContext->iformat->flags & AVFMT_TS_DISCONT) != 0 ? 10 : 3600;
    return ret;
}

AVDictionary *VideoStream::getPropertiesOfStream(AVCodecContext* cContext, AVStream* stream,
                                                 AVCodec* codec) {
    int cx;
    AVDictionary * ret = AVComponentStream::getPropertiesOfStream(cContext, stream, codec);

    av_dict_set_int(&ret, "Width", cContext->width, 0);
    av_dict_set_int(&ret, "Height", cContext->height, 0);
    av_dict_set(&ret, "Pixel format", av_get_pix_fmt_name(cContext->pix_fmt), 0);

    AVRational framerate = stream->avg_frame_rate;
    char buf[BUFFER_STRING_LENGTH];
    cx = snprintf(buf, BUFFER_STRING_LENGTH, "%.3f", framerate.num * 1.0 / framerate.den);
    if (cx >= 0 && cx < BUFFER_STRING_LENGTH) {
        av_dict_set(&ret, "Frame rate", buf, 0);
    } else {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Was not able to parse framerate from stream");
    }
    return ret;
}

bool VideoStream::canEnqueueStreamPacket(const AVPacket& packet) {
    if (mStreamIndex < 0 || mFContext->nb_streams <= mStreamIndex) {
        return false;
    }
    return AVComponentStream::canEnqueueStreamPacket(packet)
           && (getStream()->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0;
}

void VideoStream::onAVFrameReceived(AVFrame *frame) {
    frame->pts = frame->best_effort_timestamp;
}

int VideoStream::onProcessThread() {
    AVFrame* avFrame = av_frame_alloc();
    int ret;
    AVStream* stream = getStream();
    PacketQueue* queue = getPacketQueue();
    AVRational tb = stream->time_base;
    AVRational frameRate = av_guess_frame_rate(mFContext, stream, NULL);

    if (!avFrame) {
        return error(AVERROR(ENOMEM), "Cannot allocate frame for video thread");
    }

    spawnRendererThreadIfHaveNot();

    while(1) {
        // Get current frame and until error or abort
        if ((ret = decodeFrame(avFrame)) < 0) {
            av_frame_free(&avFrame);
            return ret == AVERROR_EXIT ? AVERROR_EXIT : error(ret, "Cannot decode frame");
        }

        if (ret) {
            avFrame->sample_aspect_ratio = av_guess_sample_aspect_ratio(mFContext, stream, avFrame);

            // Drop frames if allowed and falling behind master clock
            if (allowFrameDrops() && avFrame->pts != AV_NOPTS_VALUE) {
                double diff = (av_q2d(stream->time_base) * avFrame->pts) - getMasterClock()->getPts();
                if (!isnan(diff) && diff < 0 && fabs(diff) < AV_COMP_NOSYNC_THRESHOLD
                        && mPktSerial == mClock->serial()
                        && queue->numPackets()) {
                    mEarlyFrameDrops++;
                    __android_log_print(ANDROID_LOG_VERBOSE, sTag,
                                        "Early frame drop happened (Count: %d)", mEarlyFrameDrops);
                    av_frame_unref(avFrame);
                    continue;
                }
            }

            // Queue Frame for processing
            Frame* frame = mQueue->peekWritable();
            if (!frame) {
                return hasAborted()
                       ? AVERROR_EXIT : error(AVERROR_INVALIDDATA, "Failed to peek writable");
            }
            frame->setAVFrame(avFrame, frameRate, tb, mPktSerial);
            mQueue->push();
            av_frame_unref(avFrame);
        }
    }
}

int VideoStream::onRenderThread() {
    IPlayerCallback::UniqueCallback unCallback(mPlayerCallback);
    int ret;
    double remainingTime = 0;
    while (!hasAborted()) {
        // TODO wait if paused and use a condition variable to resume
        if (remainingTime > 0.0) {
//            _log("Wait video thread %lf", remainingTime);
            std::this_thread::sleep_for(
                    (std::chrono::microseconds((int64_t) (remainingTime * AV_TIME_BASE))));
        }
        remainingTime = REFRESH_RATE;
        if (!isPaused() || mForceRefresh) {
            videoProcess(&remainingTime);

            // Display the video frame
            if (mForceRefresh && mQueue->isReadIndexShown()) {
                Frame* vp = mQueue->peekLast(), *sp = NULL;
//                if (mSubStream) {       // TODO not used
//                    FrameQueue* sQueue = mSubStream->getQueue();
//                    if (sQueue->getNumRemaining() > 0) {
//                        sp = sQueue->peekFirst();
//
//                        if (vp->pts() >= sp->pts() + sp->startSubTimeMs()) {
//                            if (!sp->hasUploaded) {
//                                uint8_t * pixels[4];
//                                int pitch[4];
//                                if (!sp->width() || !sp->height()) {
//
//                                }
//                            }
//                            sp->hasUploaded = true;
//                        }
//                    }
//
//                    // TODO merge the subtitle and video streams together CONTINUE THIS
//                }

                if (!vp->hasUploaded) {
                    if (mVideoRenderer) {
//                        _log("Video render frame");
                        if ((ret = mVideoRenderer->renderFrame(vp->frame())) < 0) {
                            return error(ret, "Was not able to reader video frame");
                        }
                    } else {
                        __android_log_print(ANDROID_LOG_ERROR, sTag,
                                            "No video renderer exists to render to screen");
                    }
                    vp->hasUploaded = 1;
                    vp->flipVertical = vp->frame()->linesize[0] < 0;
                }
            }
            mForceRefresh = false;
        }
    }
    return 0;
}

int VideoStream::videoProcess(double *remainingTime) {
    int ret;
    double lastDuration, duration, delay, diff, syncThres, now;
    PacketQueue *pktQueue = getPacketQueue();
    const bool isMasterClock = getClock() == getMasterClock();

    while(1) {
        if (mQueue->getNumRemaining() > 0) {
            Frame *vp, *lastvp;

            // Dequeue the picture
            lastvp = mQueue->peekLast();
            vp = mQueue->peekFirst();

            if (vp->serial() != pktQueue->serial()) {
                // TODO i think this related to seeking?
                mQueue->peekNext();
                _log("VideoStream::videoProcess retry %ld %ld", vp->serial(), pktQueue->serial());
                continue;
            }

            if (lastvp->serial() != vp->serial()) {
                mFrameTimer = Clock::now();
            }

            if (isPaused()) {
                // Display image on page
                break;
            }

            // Calculate how off the video is from master clock
            lastDuration = getFrameDurationDiff(lastvp, vp);
            delay = lastDuration;
            if (!isMasterClock) {
                diff = mClock->getPts() - getMasterClock()->getPts();

                // Skip or repeat frame from the threshold calculated with the delay or use
                // best guess
                syncThres = AV_SYNC_THRESHOLD(delay);
                if (!isnan(diff) && fabs(diff) < mMaxFrameDuration) {
                    bool a = false;     // TODO remove this stuff
                    if (diff <= -syncThres) {
                        delay = FFMAX(0, delay + diff);
                    } else if (diff >= syncThres && delay > AV_SYNC_FRAMEDUP_THRESHOLD) {
                        delay += diff;
                    } else if (diff >= syncThres) {
                        delay *= 2;
                    } else {
                        a  =true;
                    }

                    if (!a) {
                __android_log_print(ANDROID_LOG_DEBUG, sTag, "video: delay=%0.3f A-V=%f",
                                    delay, -diff);
                    }
                }
            }
            now = Clock::now();

            if (now < mFrameTimer + delay) {
                *remainingTime = FFMIN(mFrameTimer + delay - now, *remainingTime);
                break;
            }

            mFrameTimer += delay;
            if (delay > 0 && now - mFrameTimer > AV_SYNC_THRESHOLD_MAX) {
                mFrameTimer = now;
            }

            // Sync the video clock
            {
                std::lock_guard<std::mutex> lk(mQueue->getMutex());
                if (!isnan(vp->pts())) {
                    mClock->setPts(vp->pts(), vp->serial());
                    mClock->syncToClock(getExternalClock());
                }
            }

            if (mQueue->getNumRemaining() > 1) {
                Frame* nextvp = mQueue->peekNext();
                duration = getFrameDurationDiff(vp, nextvp);
                if (!mFrameStepMode && allowFrameDrops() && now > mFrameTimer + duration) {
                    mLateFrameDrops++;
                    __android_log_print(ANDROID_LOG_VERBOSE, sTag,
                                        "Late frame drop happened (Count: %d)", mLateFrameDrops);
                    mQueue->pushNext();
                    continue;
                }
            }

            // Handle subtitles TODO this isnt used right now
            if ((ret = processSubtitleQueue()) < 0) {
                return ret;
            }

            mQueue->pushNext();
            mForceRefresh = true;
            if (mFrameStepMode && !isPaused()) {
                mCallback->togglePlayback();
            }
        }
        break;
    }
    return 0;
}

void VideoStream::spawnRendererThreadIfHaveNot() {
    if (mVideoRenderer && !hasRendererThreadStarted() && getStreamIndex() != -1) {
        spawnRenderThread();
    }
}

int VideoStream::processSubtitleQueue() {
    // TODO move this to subtitlestream and finish the code
    if (!mSubStream) {
        return 0;
    }

    Frame *sp, *sp2;
    FrameQueue* queue = mSubStream->getQueue();
    PacketQueue* pktQueue = mSubStream->getPacketQueue();
    while(queue->getNumRemaining() > 0) {
        sp = queue->peekFirst();
        if (!sp->subtitle()) {
            return error(AVERROR_INVALIDDATA, "Frame is not subtitle frame");
        }

        if (queue->getNumRemaining() > 1) {
            if (!(sp2 = queue->peekNext())->subtitle()) {
                return error(AVERROR_INVALIDDATA, "Frame is not subtitle frame");
            }
        } else {
            sp2 = NULL;
        }

        if (sp->serial() != pktQueue->serial()
                || (mClock->getPts() > (sp->pts() + sp->endSubTimeMs()))
                || (sp2 && mClock->getPts() > (sp2->pts() + sp->startSubTimeMs()))) {
            if (sp->hasUploaded) {
                for (int i = 0; i < sp->subtitle()->num_rects; i++) {
                    AVSubtitleRect* subRect = sp->subtitle()->rects[i];
                    uint8_t * pixel;
                    int pitch;

                    // TODO figure out how to do this
//                    if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
//                        for (j = 0; j < sub_rect->h; j++, pixels += pitch)
//                            memset(pixels, 0, sub_rect->w << 2);
//                        SDL_UnlockTexture(is->sub_texture);
//                    }
                }
            }
            queue->pushNext();
        } else {
            break;
        }
    }
    return 0;
}

double VideoStream::getFrameDurationDiff(Frame *frame, Frame *nextFrame) {
    if (frame->serial() != nextFrame->serial()) {
        return 0;
    }
    double duration = nextFrame->pts() - frame->pts();
    if (isnan(duration) || duration <= 0 || duration > mMaxFrameDuration) {
        return frame->duration();
    }
    return duration;
}
