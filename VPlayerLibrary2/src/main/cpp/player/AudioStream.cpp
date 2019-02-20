#include "AudioStream.h"

#define SAMPLE_QUEUE_SIZE 9
#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define FRAME_STEP_SLEEP_TIMEOUT 10

// We use about AUDIO_DIFF_AVG_NB A-V differences to make the average
#define AUDIO_DIFF_AVG_NB 20

#define _log(...) __android_log_print(ANDROID_LOG_INFO, "AudioStream", __VA_ARGS__);

const static char* sTag = "AudioStream";

AudioStream::AudioStream(AVFormatContext* context, AVPacket* flushPkt, ICallback* callback) :
        AVComponentStream(context, AVMEDIA_TYPE_AUDIO, flushPkt, callback, SAMPLE_QUEUE_SIZE),
        mAudioRenderer(NULL),
        mSwrContext(NULL),
        mAudioBuffer(NULL),
        mBufferSize(0),
        mStartPts(AV_NOPTS_VALUE),
        mLatencyInvalidated(false),
        mIsMuted(false),
        mMuteRequested(false),
        mPlaybackStateChanged(false),
        mDiffComputation(0),
        mDiffAvgCoef(exp(log(0.01) / AUDIO_DIFF_AVG_NB)),
        mDiffAvgCount(0) {
}

AudioStream::~AudioStream() {
    internalCleanUp();

    if (mAudioRenderer) {
        // Cleaned up on another thread
        mAudioRenderer->pause();
        mAudioRenderer->flush();
        mAudioRenderer = NULL;
    }
    if (mSwrContext) {
        swr_free(&mSwrContext);
        mSwrContext = NULL;
    }
    if (mAudioBuffer) {
        av_freep(&mAudioBuffer);
    }
}

void AudioStream::setPaused(bool paused) {
    mPlaybackStateChanged = true;
    AVComponentStream::setPaused(paused);
}

void AudioStream::setMute(bool mute) {
    if (mIsMuted != mute) {
        mMuteRequested = true;
        mIsMuted = mute;
    }
}

void AudioStream::invalidateLatency() {
    mLatencyInvalidated = true;
}

double AudioStream::getLatency() {
    return mAudioRenderer ? mAudioRenderer->getLatency() : 0;
}

AVDictionary *AudioStream::getPropertiesOfStream(AVCodecContext* cContext, AVStream* stream,
                                                 AVCodec* codec) {
    AVDictionary* ret = AVComponentStream::getPropertiesOfStream(cContext, stream, codec);
    av_dict_set_int(&ret, "Sample rate", cContext->sample_rate, 0);
    av_dict_set_int(&ret, "Channel(s)", cContext->channels, 0);
    av_dict_set_int(&ret, "Channel layout", cContext->channel_layout, 0);
    return ret;
}

void AudioStream::onAVFrameReceived(AVFrame *frame) {
    AVRational tb = (AVRational) { 1, frame->sample_rate };
    if (frame->pts != AV_NOPTS_VALUE) {
        frame->pts = av_rescale_q(frame->pts, mCContext->pkt_timebase, tb);
    } else if (mNextPts != AV_NOPTS_VALUE) {
        frame->pts = av_rescale_q(mNextPts, mNextPtsTb, tb);
    }
    if (frame->pts != AV_NOPTS_VALUE) {
        mNextPts = frame->pts + frame->nb_samples;
        mNextPtsTb = tb;
    }
}

void AudioStream::onDecodeFlushBuffers() {
    mNextPts = mStartPts;
    mNextPtsTb = mStartPtsTb;
}

int AudioStream::onProcessThread() {
    Frame* af;
    int ret;
    AVRational tb;
    AVFrame* avFrame = av_frame_alloc();
    if (!avFrame) {
        return error(AVERROR(ENOMEM), "Cannot allocate avframe for audio decoding");
    }
    spawnRenderThread();

    do {
        waitIfPaused();

        if ((ret = decodeFrame(avFrame)) < 0) {
            if (ret != AVERROR_EXIT) {
                error(ret, "Cannot decode audio frame");
            }
            break;
        } else if (ret) {
            tb = (AVRational) {1, avFrame->sample_rate};

            if (!(af = mQueue->peekWritable())) {
                ret = hasAborted()
                      ? AVERROR_EXIT : error(AVERROR_INVALIDDATA, "Failed to peek writable");
                break;
            }
            af->setAVFrame(avFrame, (AVRational) {avFrame->nb_samples, avFrame->sample_rate},
                           tb, mPktSerial);
            mQueue->push();
        }
    } while(ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
    av_frame_free(&avFrame);
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "onProcessThread end");
    return ret;
}

int AudioStream::onRenderThread() {
    if ((mAudioRenderer = mCallback->createAudioRenderer(mCContext)) == NULL) {
        return error(AVERROR(ENOMEM), "Cannot create audio renderer");
    }

    Frame *frame = nullptr;
    while (!hasAborted()) {
        AVFrame *af;

        // Pause if needed
        if (mPlaybackStateChanged) {
            if (isPaused()) {
                mAudioRenderer->pause();
            } else {
                mAudioRenderer->play();
            }
            mPlaybackStateChanged = false;
        }
        waitIfRenderPaused();
        if (hasAborted()) {
            break;
        }

        // After paused, re-enable renderer and update the clock to new time
        if (mPlaybackStateChanged) {
           if (frame) {
                updateClock(frame, Clock::now());
                mAudioRenderer->play();
            }
            mPlaybackStateChanged = false;
        }

        // Mute if needed
        if (mMuteRequested) {
            if (mIsMuted) {
                mAudioRenderer->setVolume(0);
            } else {
                mAudioRenderer->setVolume(1);
            }
            mMuteRequested = false;
        }

        double frameDecodeStart = Clock::now();

        // Get next frame
        do {
            if (!(frame = mQueue->peekReadable())) {
                if (hasAborted()) {
                    return AVERROR_EXIT;
                }
                return error(-1, "Unable to get audio frame from queue");
            }
            mQueue->pushNext();
            // TODO should we wait on fail? seek prob
        } while (frame->serial() != mPacketQueue->serial());

        af = frame->frame();

        // When frame-stepping, prevent audio to over-write the master clock (assuming video clock)
        // and therefore sleep the thread until next audio buffer can be written
        if (mCallback->inFrameStepMode() && getMasterClock() != getClock()
                && getClock()->getPts() > getMasterClock()->getPts()) {
            double beforePauseTime = Clock::now();
            mAudioRenderer->pause();
            while (getClock()->getPts() > getMasterClock()->getPts()
                   || getMasterClock() != getClock()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_STEP_SLEEP_TIMEOUT));
                if (hasAborted()) {
                    return 0;
                }
            }
            mAudioRenderer->play();
            frameDecodeStart -= (Clock::now() - beforePauseTime);
        }

        int wantedNbSamples = syncClocks(af);

        // Write audio to renderer if not muted
        if (!mIsMuted) {
            uint8_t *audioData;
            int size, written = 0;
            if ((size = decodeAudioFrame(af, wantedNbSamples, &audioData)) < 0) {
                // Failed to decode frame
                break;
            }

            // If renderer only writes partial audio then loop till all is written
            while (written < size) {
                int ret = mAudioRenderer->write(audioData + written, size);
                if (hasAborted()) {
                    return 0;
                }
                if (ret < 0) {
                    return error(ret, "Failed to write from audio renderer");
                } else if (ret == 0 && written == 0) {
                    // TODO if this gets hit a lot on purpose remove this
                    return error(-1, "Failed to write any data to audio renderer");
                }
                written += ret;
                size -= FFMAX(ret, 0);
            }
        } else {
            // Calculate the time to wait for a write operation to normally finish
            int time = (int) (wantedNbSamples * (1000.0 / mAudioRenderer->sampleRate()));
            std::this_thread::sleep_for(std::chrono::milliseconds(time));
        }

        // Update audio clock
        updateClock(frame, frameDecodeStart);
    }
    return 0;
}

int AudioStream::decodeAudioFrame(AVFrame* af, int wantedNbSamples, uint8_t **out) {
    int ret, len, size;
    int numChannels = mAudioRenderer->numChannels();
    int sampleRate = mAudioRenderer->sampleRate();
    int64_t layout = mAudioRenderer->layout();
    enum AVSampleFormat format = mAudioRenderer->format();

    // Create swr if needs conversion
    int64_t inLayout = (af->channel_layout
                        && af->channels == av_get_channel_layout_nb_channels(af->channel_layout))
                       ? (int64_t) af->channel_layout : av_get_default_channel_layout(af->channels);

    if (af->format != format
        || layout != inLayout
        || af->sample_rate != sampleRate
        || (wantedNbSamples != af->nb_samples && !mSwrContext)) {
        swr_free(&mSwrContext);
        mSwrContext = swr_alloc_set_opts(NULL, layout, format, sampleRate, inLayout,
                                         (AVSampleFormat) af->format, af->sample_rate, 0,
                                         NULL);
        if (!mSwrContext) {
            return error(AVERROR(ENOMEM), "Unable to create audio swr");
        } else if ((ret = swr_init(mSwrContext)) < 0) {
            swr_free(&mSwrContext);
            return error(ret, "Failed to ini audio swr");
        }
    }

    if (mSwrContext) {
        // Prepare the buffer
        float sampleDivision = (float) sampleRate / af->sample_rate;
        int outCount = (int) (wantedNbSamples * sampleDivision + 256);
        int outSize = av_samples_get_buffer_size(NULL, numChannels, outCount, format, 0);
        if (mBufferSize < outSize) {
            if (outSize < 0) {
                return error(-1, "av_samples_get_buffer_size() failed");
            }
            if (wantedNbSamples != af->nb_samples) {
                if (swr_set_compensation(mSwrContext,
                                         (int) ((wantedNbSamples - af->nb_samples) *
                                                sampleDivision),
                                         (int) (wantedNbSamples * sampleDivision)) < 0) {
                    return error(-1, "swr_set_compensation() failed");
                }
            }
            av_fast_malloc(&mAudioBuffer, &mBufferSize, (size_t) outSize);
        }
        if (mAudioBuffer == NULL) {
            return error(AVERROR(ENOMEM), "Cannot create audio buffer");
        }
        if ((len = swr_convert(mSwrContext, &mAudioBuffer, outCount,
                               (const uint8_t **) af->data, af->nb_samples)) < 0) {
            return error(-1, "swr_convert() failed");
        }
        if (len == outCount) {
            __android_log_print(ANDROID_LOG_WARN, sTag, "audio buffer is probably too small");
            if (swr_init(mSwrContext) < 0) {
                swr_free(&mSwrContext);
            }
        }
        *out = mAudioBuffer;
        size = len * numChannels * av_get_bytes_per_sample(format);
    } else {
        *out = af->data[0];
        size = av_samples_get_buffer_size(NULL, af->channels, af->nb_samples,
                                          (AVSampleFormat) af->format, 1);
    }
    return size;
}

int AudioStream::syncClocks(AVFrame* frame) {
    int numSamples = frame->nb_samples;
    int wantedSamples = numSamples;
    if (getMasterClock() != getClock()) {
        double diff, avgDiff;
        int minNumSamples, maxNumSamples;
        double diffThreshold = (double) numSamples / frame->sample_rate;

        diff = getClock()->getPts() - getMasterClock()->getPts();

        if (!isnan(diff) && fabs(diff) < AV_COMP_NOSYNC_THRESHOLD) {
            mDiffComputation = diff + mDiffAvgCoef * mDiffComputation;          // TODO test this
            if (mDiffAvgCount < AUDIO_DIFF_AVG_NB) {
                // not enough measures to have a correct estimate
                mDiffAvgCount++;
            } else {
                // estimate the A-V difference
                avgDiff = mDiffComputation * (1.0 - mDiffAvgCoef);

                if (fabs(avgDiff) >= diffThreshold) {
                    wantedSamples = numSamples + (int) (diff * mAudioRenderer->sampleRate());
                    minNumSamples = ((numSamples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    maxNumSamples = ((numSamples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wantedSamples = av_clip(wantedSamples, minNumSamples, maxNumSamples);
                }

                if (!mCallback->inFrameStepMode()) {
                    __android_log_print(ANDROID_LOG_VERBOSE, sTag,
                                        "diff=%f adiff=%f sample_diff=%d %f", diff, avgDiff,
                                        wantedSamples - numSamples, diffThreshold);
                }
            }
        } else {
            // Too big difference : may be initial PTS errors, so reset A-V filter
            mDiffAvgCount = 0;
            mDiffComputation = 0;
        }
    }
    return wantedSamples;
}

void AudioStream::updateClock(Frame *frame, double time) {
    std::lock_guard<std::mutex> lk(mQueue->getMutex());
    if (!isnan(frame->pts())) {
        getClock()->setTimeAt(frame->pts(), time, frame->serial());
        getExternalClock()->syncToClock(getClock());
        mAudioRenderer->updateLatency(mLatencyInvalidated);
        mLatencyInvalidated = false;
    }
}
