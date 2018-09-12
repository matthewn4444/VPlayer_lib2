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
        mInvalidateSubs(false),
        mNextFrameWritten(false),
        mVideoRenderer(NULL),
        mForceRefresh(false),
        mFrameTimer(0),
        mSubStream(NULL),
        mEarlyFrameDrops(0),
        mMaxFrameDuration(0),
        mLateFrameDrops(0),
        mCanSupportNetworkControls(false),
        mCSConverter(NULL) {
}

VideoStream::~VideoStream() {
    if (mCSConverter) {
        delete mCSConverter;
        mCSConverter = NULL;
    }
}

void VideoStream::setPaused(bool paused) {
    if (!paused) {
        mFrameTimer += mClock->getTimeSinceLastUpdate();
        if (mCanSupportNetworkControls) {
            mClock->paused = false;
        }
        mClock->updatePts();
    }
    AVComponentStream::setPaused(paused);
}


void VideoStream::setVideoRenderer(IVideoRenderer *videoRenderer) {
    mVideoRenderer = videoRenderer;
    spawnRendererThreadIfHaveNot();
}

void VideoStream::setSubtitleComponent(SubtitleStream *stream) {
    mSubStream = stream;
}

void VideoStream::setSupportNetworkControls(bool flag) {
    mCanSupportNetworkControls = flag;
}

void VideoStream::invalidNextFrame() {
    mNextFrameWritten = false;
    mInvalidateSubs = true;
}

bool VideoStream::allowFrameDrops() {
    return mAllowDropFrames && getClock() != getMasterClock();
}

float VideoStream::getAspectRatio() {
    return (float) mCContext->width / mCContext->height;
}

int VideoStream::open() {
    int ret = AVComponentStream::open();
    if (ret < 0) {
        return ret;
    }
    mForceRefresh = false;
    mMaxFrameDuration = (mFContext->iformat->flags & AVFMT_TS_DISCONT) != 0 ? 10 : 3600;

    // Init the pool to fit the size of the video frames
    if ((ret = mFramePool.resize(mQueue->capacity(), mCContext->width, mCContext->height,
                                 AV_PIX_FMT_RGBA)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Unable to create video frame pool");
    }

    // If higher than 8 bit, use a 16bit converter for faster conversion
    const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(mCContext->pix_fmt);
    if (mCSConverter) {
        delete mCSConverter;
        mCSConverter = NULL;
    }
    if (descriptor->comp->depth > 8) {
        if (mCSConverter) {
            delete mCSConverter;
        }

        // Only handling basic 10, 12 and 16 bit YUV data
        // 16 bit and big endian are untested, assuming from docs it will work, ffmpeg cannot encode
        // either options to test
        switch (mCContext->pix_fmt) {
            case AV_PIX_FMT_YUV420P16BE:
            case AV_PIX_FMT_YUV420P16LE:
            case AV_PIX_FMT_YUV420P10BE:
            case AV_PIX_FMT_YUV420P10LE:
            case AV_PIX_FMT_YUV420P12BE:
            case AV_PIX_FMT_YUV420P12LE:
                mCSConverter = new YUV16to8Converter(mCContext, AV_PIX_FMT_YUV420P);
                break;
            case AV_PIX_FMT_YUV422P16BE:
            case AV_PIX_FMT_YUV422P16LE:
            case AV_PIX_FMT_YUV422P10BE:
            case AV_PIX_FMT_YUV422P10LE:
            case AV_PIX_FMT_YUV422P12BE:
            case AV_PIX_FMT_YUV422P12LE:
                mCSConverter = new YUV16to8Converter(mCContext, AV_PIX_FMT_YUV422P);
                break;
            case AV_PIX_FMT_YUV444P16BE:
            case AV_PIX_FMT_YUV444P16LE:
            case AV_PIX_FMT_YUV444P10BE:
            case AV_PIX_FMT_YUV444P10LE:
            case AV_PIX_FMT_YUV444P12LE:
            case AV_PIX_FMT_YUV444P12BE:
                mCSConverter = new YUV16to8Converter(mCContext, AV_PIX_FMT_YUV444P);
                break;
            default:
                __android_log_print(ANDROID_LOG_WARN, sTag,
                                    "Cannot process 9-16 bit of pix format %s, using slow routine",
                                    av_get_pix_fmt_name(mCContext->pix_fmt));
                break;
        }
    }
    if (mCSConverter == NULL) {
        mCSConverter = new BasicYUVConverter();
    }
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
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "onProcessThread video started");
    AVFrame* avFrame = av_frame_alloc(), *rgbaFrame;
    int ret;
    AVStream* stream = getStream();
    AVRational tb = stream->time_base;
    AVRational frameRate = av_guess_frame_rate(mFContext, stream, NULL);

    if (!avFrame) {
        return error(AVERROR(ENOMEM), "Cannot allocate frame for video thread");
    }
    spawnRendererThreadIfHaveNot();

    while (1) {
        waitIfPaused();

        // Get current frame and until error or abort
        if ((ret = decodeFrame(avFrame)) < 0) {
            if (ret != AVERROR_EXIT) {
                error(ret, "Cannot decode frame");
            }
            break;
        } else if (!ret) {
            // Not enough packets to make a frame
            continue;
        }

        avFrame->sample_aspect_ratio = av_guess_sample_aspect_ratio(mFContext, stream, avFrame);

        // Drop frames if allowed and falling behind master clock
        if (allowFrameDrops() && avFrame->pts != AV_NOPTS_VALUE) {
            double audioLatency = mCallback->getAudioLatency();
            double diff = (av_q2d(stream->time_base) * avFrame->pts)
                          - (getMasterClock()->getPts() - audioLatency);
            if (!isnan(diff) && diff < 0 && fabs(diff) < AV_COMP_NOSYNC_THRESHOLD
                    && mPktSerial == mClock->serial()
                    && mPacketQueue->numPackets()) {
                mEarlyFrameDrops++;
                __android_log_print(ANDROID_LOG_VERBOSE, sTag,
                                    "Early frame drop happened (Count: %d)", mEarlyFrameDrops);
                if (mSubStream) {
                    mSubStream->getPendingSubtitleFrame(avFrame->pts);
                }
                av_frame_unref(avFrame);
                continue;
            }
        }

        // Convert the decoded frame into rgba and composite subtitles if needed
        if ((ret = processVideoFrame(avFrame, &rgbaFrame)) < 0) {
            break;
        }

        // Put the final rbga frame into the queue for rendering
        Frame* frame = mQueue->peekWritable();
        if (!frame) {
            ret = hasAborted() ? AVERROR_EXIT : error(AVERROR_INVALIDDATA, "Failed peek");
            break;
        }
        frame->setAVFrame(rgbaFrame, frameRate, tb, mPktSerial);
        mQueue->push();
        av_frame_unref(avFrame);
        av_frame_unref(rgbaFrame);
    }
    av_frame_free(&avFrame);
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "onProcessThread video ended");
    return ret;
}

int VideoStream::onRenderThread() {
    IPlayerCallback::UniqueCallback unCallback(mPlayerCallback);
    int ret;
    double remainingTime = 0;
    while (!hasAborted()) {
        if (remainingTime > 0.0) {
//            _log("Wait video thread %lf", remainingTime);
            std::this_thread::sleep_for(
                    (std::chrono::microseconds((int64_t) (remainingTime * AV_TIME_BASE))));
        }

        waitIfRenderPaused();

        remainingTime = REFRESH_RATE;
        if (!isPaused()) {
            if (isRealTime() && getMasterClock() == getExternalClock()) {
                mCallback->updateExternalClockSpeed();
            }
            synchronizeVideo(&remainingTime);

            bool readyToRender = mForceRefresh && mQueue->isReadIndexShown();

            // Write frame to renderer before render time (or at render time) to avoid extra delay
            // from posting frame to display since this operation can take a couple of ms
            if (!mNextFrameWritten && mVideoRenderer) {
                if (readyToRender) {
                    // Late frame write, might be from dropped frames or heavy subtitles render
                    writeFrameToRender(mQueue->peekLast()->frame());
                    mNextFrameWritten = true;
                } else if (mQueue->getNumRemaining() > 0) {
                    writeFrameToRender(mQueue->peekFirst()->frame());
                    mNextFrameWritten = true;

                    // Writing frame might take some time so re-evaluate the remaining time
                    synchronizeVideo(&remainingTime);
                    readyToRender = mForceRefresh && mQueue->isReadIndexShown();
                }
            }

            // Display the video frame
            if (readyToRender) {
                Frame *vp = mQueue->peekLast();
                if (mVideoRenderer) {
                    if ((ret = mVideoRenderer->renderFrame()) < 0) {
                        return error(ret, "Was not able to reader video frame");
                    }
                } else {
                    __android_log_print(ANDROID_LOG_ERROR, sTag,
                                        "No video renderer exists to render to screen");
                }
                mNextFrameWritten = false;
                vp->flipVertical = vp->frame()->linesize[0] < 0;

                // Frame has been rendered and used, recycle it
                mFramePool.recycle(vp->frame());
            }
            mForceRefresh = false;
        }
    }
    return 0;
}

int VideoStream::processVideoFrame(AVFrame* avFrame, AVFrame** outFrame) {
    int ret;
    AVFrame* tmpFrame = mFramePool.acquire();
    tmpFrame->pts = avFrame->pts;
    tmpFrame->pkt_pos = avFrame->pkt_pos;
    tmpFrame->sample_aspect_ratio = avFrame->sample_aspect_ratio;

    // Convert the frame to rgba
    if ((ret = mCSConverter->convert(avFrame, tmpFrame)) < 0) {
        return ret;
    }

    // Process the subtitle frame if it exists
    if (!hasAborted() && mSubStream) {
        const double clockPts = getClock()->getPts();
        if (mVideoRenderer != NULL && mVideoRenderer->writeSubtitlesSeparately()) {
            if (mSubStream->prepareSubtitleFrame(avFrame->pts, clockPts, mInvalidateSubs) < 0) {
                __android_log_print(ANDROID_LOG_WARN, sTag, "Failed to prepare subtitle frames");
            }
        } else {
            // Blend to video video frame
            if (mSubStream->blendToFrame(tmpFrame, clockPts, true) < 0) {
                __android_log_print(ANDROID_LOG_WARN, sTag, "Failed to blend subs to video frame");
            }
        }
    }
    mInvalidateSubs = false;
    *outFrame = tmpFrame;
    return 0;
}

int VideoStream::synchronizeVideo(double *remainingTime) {
    double lastDuration, duration, delay, diff, syncThres, now;
    const bool isMasterClock = getClock() == getMasterClock();

    while (1) {
        if (mQueue->getNumRemaining() > 0) {
            Frame *vp, *lastvp;

            // Dequeue the picture
            lastvp = mQueue->peekLast();
            vp = mQueue->peekFirst();

            // Just performed seek, remove all old frames after packet queue flushed
            if (vp->serial() != mPacketQueue->serial()) {
                if (mSubStream) {
                    mSubStream->getPendingSubtitleFrame(vp->frame()->pts);
                }
                mFramePool.recycle(vp->frame());
                mQueue->pushNext();
                _log("VideoStream::videoProcess retry %ld %ld", vp->serial(),
                     mPacketQueue->serial());
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
                double audioLatency = mCallback->getAudioLatency();
                diff = mClock->getPts() - (getMasterClock()->getPts() - audioLatency);

                // Skip or repeat frame from the threshold calculated with the delay or use
                // best guess
                syncThres = AV_SYNC_THRESHOLD(delay);
                if (!isnan(diff) && fabs(diff) < mMaxFrameDuration) {
                    if (diff <= -syncThres) {
                        delay = FFMAX(0, delay + diff);
                    } else if (diff >= syncThres && delay > AV_SYNC_FRAMEDUP_THRESHOLD) {
                        delay += diff;
                    } else if (diff >= syncThres) {
                        delay *= 2;
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
                    getExternalClock()->syncToClock(mClock);
                }
            }

            // Drop frames if processing is behind on render thread
            if (mQueue->getNumRemaining() > 1) {
                Frame* nextvp = mQueue->peekNext();
                duration = getFrameDurationDiff(vp, nextvp);
                if (!mFrameStepMode && allowFrameDrops() && now > mFrameTimer + duration) {
                    mLateFrameDrops++;
                    __android_log_print(ANDROID_LOG_VERBOSE, sTag,
                                        "Late frame drop happened (Count: %d)", mLateFrameDrops);
                    mQueue->pushNext();
                    if (mSubStream) {
                        mSubStream->getPendingSubtitleFrame(vp->frame()->pts);
                    }
                    mFramePool.recycle(vp->frame());
                    continue;
                }
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

int VideoStream::writeFrameToRender(AVFrame* frame) {
    int ret = 0;

    // If subtitles are written to a separate layer, get the pending frame
    AVFrame* subFrame = NULL;
    if (mSubStream != NULL && mVideoRenderer->writeSubtitlesSeparately()) {
        subFrame = mSubStream->getPendingSubtitleFrame(frame->pts);
    }

    // Write frame (video and subtitles) to renderer
    if ((ret = mVideoRenderer->writeFrame(frame, subFrame)) < 0) {
        return error(ret, "Was not able to write to video frame");
    }
    return ret;
}
