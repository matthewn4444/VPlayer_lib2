#include "Player.h"

extern "C" {
#include <libavutil/opt.h>
}

#define EXTCLK_MIN_FRAMES 2
#define EXTCLK_MAX_FRAMES 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTCLK_SPEED_MIN  0.900
#define EXTCLK_SPEED_MAX  1.010
#define EXTCLK_SPEED_STEP 0.001

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define sTag "NativePlayer"

static int decode_interrupt_callback(void *player) {
    return reinterpret_cast<Player *>(player)->isAborting() ? AVERROR_EXIT : 0;
}

static void freeAVDictionaryList(AVDictionary **list, size_t size) {
    for (int i = 0; i < size; ++i) {
        av_dict_free(&list[i]);
    }
    av_freep(&list);
}

static bool streamTypeExists(AVFormatContext *context, AVMediaType type) {
    for (int i = 0; i < context->nb_streams; ++i) {
        if (context->streams[i]->codecpar->codec_type == type) {
            return true;
        }
    }
    return false;
}

Player::Player() :
        mCallback(NULL),
        mReadThreadId(NULL),
        mVideoStream(NULL),
        mAudioStream(NULL),
        mSubtitleStream(NULL),
        mVideoRenderer(NULL),
        mSubtitleFrameWidth(0),
        mSubtitleFrameHeight(0),
        mFilepath(NULL),
        mShowVideo(true),
        mAbortRequested(false),
        mSeekRequested(false),
        mAttachmentsRequested(false),
        mIsPaused(false),
        mLastPaused(false),
        mReadPauseRet(0),
        mSeekPos(0),
        mSeekRel(0),
        mIsEOF(false),
        mInfiniteBuffer(false) {
    avformat_network_init();

    av_init_packet(&mFlushPkt);
    mFlushPkt.size = 0;
    mFlushPkt.data = (uint8_t *) &mFlushPkt;
}

Player::~Player() {
    reset();
    setCallback(NULL);
    avformat_network_deinit();
}

bool Player::openVideo(const char *stream_file_url) {
    reset();
    mShowVideo = true;
    mFilepath = av_strdup(stream_file_url);
    mReadThreadId = new std::thread(&Player::readThread, this);
    return true;
}

void Player::stepNextFrame() {
    // Step after un-pausing the playback
    if (mIsPaused) {
        togglePlayback();
    }
    if (mVideoStream) {
        mVideoStream->setFrameStepMode(true);
    } else {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Setting frame step mode without video stream");
    }
}

void Player::setVideoRenderer(IVideoRenderer *videoRenderer) {
    mVideoRenderer = videoRenderer;
    if (mVideoStream && videoRenderer) {
        mVideoStream->setVideoRenderer(videoRenderer);
    }
}

void Player::togglePlayback() {
    for (int i = 0; i < mAVComponents.size(); i++) {
        mAVComponents[i]->setPaused(mIsPaused, mReadPauseRet);
    }
    mExtClock.updatePts();
    mIsPaused = mExtClock.paused = !mIsPaused;
}

IAudioRenderer *Player::getAudioRenderer(AVCodecContext* context) {
    return mCallback->getAudioRenderer(context);
}

Clock *Player::getMasterClock() {
    if (mAudioStream) {
        return mAudioStream->getClock();
    }
    return &mExtClock;
}

Clock *Player::getExternalClock() {
    return &mExtClock;
}

void Player::updateExternalClockSpeed() {
    int vNumPackets = mVideoStream ? mVideoStream->getPacketQueue()->numPackets() : -1;
    int aNumPackets = mAudioStream ? mAudioStream->getPacketQueue()->numPackets() : -1;
    if ((0 <= vNumPackets && vNumPackets < EXTCLK_MIN_FRAMES)
            || (0 <= aNumPackets && aNumPackets < EXTCLK_MIN_FRAMES)) {
        mExtClock.setSpeed(FFMAX(EXTCLK_SPEED_MIN, mExtClock.speed - EXTCLK_SPEED_STEP));
    } else if (vNumPackets > EXTCLK_MAX_FRAMES && aNumPackets > EXTCLK_MAX_FRAMES) {
        mExtClock.setSpeed(FFMAX(EXTCLK_SPEED_MAX, mExtClock.speed - EXTCLK_SPEED_STEP));
    } else {
        double speed = mExtClock.speed;
        if (speed != 1.0) {
            mExtClock.setSpeed(speed + EXTCLK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
        }
    }
}

void Player::onQueueEmpty(StreamComponent *component) {
    mReadThreadCondition.notify_one();
}

void Player::abort() {
    mAbortRequested = true;
    for (int i = 0; i < mAVComponents.size(); ++i) {
        mAVComponents[i]->abort();
    }
}

void Player::setSubtitleFrameSize(int width, int height) {
    if (width > 0 && height > 0) {
        mSubtitleFrameWidth = width;
        mSubtitleFrameHeight = height;
        if (mSubtitleStream) {
            mSubtitleStream->setFrameSize(width, height);
        }
    }
}

void Player::setCallback(IPlayerCallback *callback) {
    std::lock_guard<std::mutex> lk(mErrorMutex);
    mCallback = callback;

    for (int i = 0; i < mAVComponents.size(); i++) {
        mAVComponents[i]->setCallback(callback);
    }
}

void Player::reset() {
    abort();
    if (mReadThreadId && mReadThreadId->get_id() != std::this_thread::get_id()) {
        mReadThreadCondition.notify_all();
        __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Waiting to join read thread...");
        mReadThreadId->join();
        delete mReadThreadId;
        mReadThreadId = NULL;
    }

    // Close stream
    if (mVideoStream) {
        delete mVideoStream;
        mVideoStream = NULL;
    }
    if (mAudioStream) {
        delete mAudioStream;
        mAudioStream = NULL;
    }
    if (mSubtitleStream) {
        delete mSubtitleStream;
        mSubtitleStream = NULL;
    }
    mAVComponents.clear();
    if (mFilepath) {
        av_freep(&mFilepath);        // TODO check if this is needed
    }
    mAbortRequested = false;
}

int Player::error(int errorCode, const char *message) {
    std::lock_guard<std::mutex> lk(mErrorMutex);
    if (mCallback) {
        mCallback->onError(errorCode, sTag, message);
    }
    return errorCode;
}

/* This is threaded */
void Player::readThread() {
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Start of read thread");

    IPlayerCallback::UniqueCallback unCallback(mCallback);
    if (!unCallback.callback()) {
        return;
    }

    AVFormatContext *context = avformat_alloc_context();
    if (!context) {
        error(AVERROR(ENOMEM), "Could not allocate format context");
        return;
    }
    context->interrupt_callback.callback = decode_interrupt_callback;
    context->interrupt_callback.opaque = this;

    AVDictionary *format_opts = NULL;

    // Open the stream
    av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
    int err = avformat_open_input(&context, mFilepath, NULL, NULL);
    av_dict_free(&format_opts);
    if (err < 0) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot open file path/stream: %s", mFilepath);
        error(err, "Cannot open file/stream, does not exist");
    } else {
        if (tOpenStreams(context) >= 0) {
            tReadLoop(context);
        }
    }
    avformat_close_input(&context);
    avformat_free_context(context);
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "End of read thread");
    reset();
}

int Player::tOpenStreams(AVFormatContext *context) {
    int err, i;
    av_format_inject_global_side_data(context);

    // Get the info for the stream
    if ((err = avformat_find_stream_info(context, NULL)) < 0) {
        return err;
    }

    for (i = 0; i < context->nb_streams; ++i) {
        context->streams[i]->discard = AVDISCARD_ALL;
    }

    // Detect which streams are available and create components for them
    if (streamTypeExists(context, AVMEDIA_TYPE_AUDIO)) {
        mAudioStream = new AudioStream(context, &mFlushPkt, this);
        mAudioStream->setCallback(mCallback);
    }

    if (mShowVideo && streamTypeExists(context, AVMEDIA_TYPE_VIDEO)) {
        mVideoStream = new VideoStream(context, &mFlushPkt, this);
        mVideoStream->setCallback(mCallback);
        if (mVideoRenderer) {
            mVideoStream->setVideoRenderer(mVideoRenderer);
        }
        if (streamTypeExists(context, AVMEDIA_TYPE_SUBTITLE)) {
            mSubtitleStream = new SubtitleStream(context, &mFlushPkt, this);
            mSubtitleStream->setCallback(mCallback);
            mVideoStream->setSubtitleComponent(mSubtitleStream);
        }
        mAttachmentsRequested = true;
    }

    if (!mVideoStream && !mAudioStream) {
        return error(AVERROR_STREAM_NOT_FOUND, "Failed to open file, stream invalid");
    }
    mInfiniteBuffer = mVideoStream ? mVideoStream->isRealTime() : mAudioStream->isRealTime();

    if (mVideoStream) {
        mAVComponents.push_back((StreamComponent *) mVideoStream);
    }
    if (mAudioStream) {
        mAVComponents.push_back((StreamComponent *) mAudioStream);
    }
    if (mSubtitleStream) {
        mAVComponents.push_back((StreamComponent *) mSubtitleStream);
    }

    // Send metadata of the video streams and container
    if (mCallback && (err = sendMetadataReady(context)) < 0) {
        return err;
    }

    // Open streams and start decoding
    if (mVideoStream && (err = mVideoStream->pickBest()) < 0) {
        return error(err, "Cannot choose/open best video stream");
    }
    int vIndex = mVideoStream ? mVideoStream->getStreamIndex() : -1;
    if (mAudioStream && (err = mAudioStream->pickBest(vIndex)) < 0) {
        return error(err, "Cannot choose/open best audio stream");
    }
    if (mShowVideo) {
        int aIndex = mAudioStream ? mAudioStream->getStreamIndex() : -1;
        if (mSubtitleStream) {
            if ((err = mSubtitleStream->pickBest(aIndex >= 0 ? aIndex : vIndex)) < 0) {
                return error(err, "Cannot choose/open best subtitle stream");
            }
            int width = mSubtitleFrameWidth;
            int height = mSubtitleFrameHeight;

            // Get the max width and height with video aspect ratio
            if (mVideoStream) {
                const float ratio = mVideoStream->getAspectRatio();
                if ((float) width / height > ratio) {
                    width = (int) (height * ratio);
                } else {
                    height = (int) (height / ratio);
                }
            }
            mSubtitleStream->setFrameSize(width, height);
        }
    }
    return 0;
}

int Player::tReadLoop(AVFormatContext *context) {
    int ret = 0, count;
    bool isFull;
    AVStream *vStream;
    AVPacket pkt;
    std::mutex waitMutex;
    bool handled;
    mCallback->onStreamReady();

    while (!mAbortRequested) {
        if (mIsPaused != mLastPaused) {
            // TODO can we simplify the paused logic?, may be used for rtmp
            mLastPaused = mIsPaused;
            if (mIsPaused) {
                mReadPauseRet = av_read_pause(context);
            } else {
                av_read_play(context);
            }
        }

#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        if (mIsPaused && (!strcmp(context->iformat->name, "rtsp") ||
                          (context->pb && !strncmp(mFilepath, "mmsh:", 5)))) {
            // wait 10 ms to avoid trying to get another packet, from ffplay.c
            sleepMs(10);
            continue;
        }
#endif

        // Handle seek
        if (mSeekRequested) {
            int64_t seekTarget = mSeekPos;
            int64_t seekMin = mSeekRel > 0 ? seekTarget - mSeekRel + 2 : INT64_MIN;
            int64_t seekMax = mSeekRel < 0 ? seekTarget - mSeekRel - 2 : INT64_MAX;
// FIXME the +-2 is due to rounding being not done in the correct direction in generation
//      of the seek_pos/seek_rel variables, from ffplay.c

            if ((ret = avformat_seek_file(context, -1, seekMin, seekTarget, seekMax, 0)) < 0) {
                __android_log_print(ANDROID_LOG_ERROR, sTag, "%s: error in seeking", mFilepath);
            } else {
                for (StreamComponent *c : mAVComponents) {
                    if ((ret = c->getPacketQueue()->flushPackets(&mFlushPkt)) < 0) {
                        return error(ret, "Seek failed because could not flush packets");
                    }
                }
                mExtClock.setPts(seekTarget / (double) AV_TIME_BASE);
            }
            mSeekRequested = false;
            mAttachmentsRequested = true;
            mIsEOF = false;
            if (mIsPaused) {
                stepNextFrame();
            }
        }

        // Send attachments to video queue
        if (mAttachmentsRequested) {
            if (mVideoStream != NULL && (vStream = mVideoStream->getStream())
                && vStream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket copy = {0};
                if ((ret = av_packet_ref(&copy, &vStream->attached_pic)) < 0) {
                    return error(ret);
                }
                if ((ret = mVideoStream->getPacketQueue()->enqueue(&copy)) < 0) {
                    return error(ret);
                }
                if ((ret = mVideoStream->getPacketQueue()->enqueueEmpty()) < 0) {
                    return error(ret);
                }
            }
            mAttachmentsRequested = false;
        }

        if (mAbortRequested) {
            break;
        }

        // Cooldown if queues are full and continue after 10sec sleep or by condition
        count = 0;
        isFull = true;
        for (StreamComponent *c : mAVComponents) {
            count += c->getPacketQueue()->size();
            if (isFull) {
                isFull = c->isQueueFull();
            }
        }
        if ((!mInfiniteBuffer && count > MAX_QUEUE_SIZE) || isFull) {
            // TODO if paused and full, maybe we should just wait for a long time and not timeout
            std::unique_lock<std::mutex> lk(waitMutex);
            mReadThreadCondition.wait_for(lk, (std::chrono::milliseconds(10)));
            continue;
        }

        // Detect if we have finished
        if ((!mAudioStream || mAudioStream->isFinished())
                && (!mVideoStream || mVideoStream->isFinished())) {
            __android_log_print(ANDROID_LOG_VERBOSE, sTag, "I am done");
            if (!mIsPaused) {
                togglePlayback();
            }
            mCallback->onStreamFinished();
            // TODO handle this case, maybe using cond variable and wait for seek to wake up
            return 0;       // TODO remove this when testing end video case
        }

        // Read the incoming packet
        if ((ret = av_read_frame(context, &pkt)) < 0) {
            if ((ret == AVERROR_EOF) || (avio_feof(context->pb) && !mIsEOF)) {
// TODO check if this is needed, seems just to ping queues, remove in the future
//                for (StreamComponent *c : mAVComponents) {
//                    if ((ret = c->getPacketQueue()->enqueueEmpty()) < 0) {
//                        return error(ret);
//                    }
//                }
                mIsEOF = true;
            }
            if (context->pb && context->pb->error) {
                error(context->pb->error);
                break;
            }
            // TODO is this really needed anymore?
//            std::unique_lock<std::mutex> lk(waitMutex);
//            mReadThreadCondition.wait_for(lk, (std::chrono::milliseconds(10)));
            _log("Waiting cuz error or everything is done parsing, wake me up when needed (seek)");
            std::unique_lock<std::mutex> lk(waitMutex);
            mReadThreadCondition.wait(lk);
            continue;
        } else {
            mIsEOF = false;
        }

        // Check if the packet can be handled by a stream component
        handled = false;
        if (pkt.pts >= 0) {
            for (StreamComponent *c : mAVComponents) {
                if (c->canEnqueueStreamPacket(pkt)) {
//                    _log("    Send packet to %ld | %ld | %d | %s", pkt.pts, pkt.duration, pkt.size,
//                         c->typeName());
                    // TODO check the pts and drop all frames if pts is behind main clock, test by removing neon
                    if ((ret = c->getPacketQueue()->enqueue(&pkt)) < 0) {
                        return error(ret, "Cannot enqueue packet to stream component");
                    }
                    handled = true;
                    break;
                }
            }
        }
        if (!handled) {
            if (context->streams[pkt.stream_index]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
                __android_log_print(ANDROID_LOG_VERBOSE, sTag, "This packet is not handled %i %ld",
                                    pkt.stream_index, pkt.pts);
            }
            av_packet_unref(&pkt);
        }
        ret = 0;
    }
    return ret;
}

int Player::sendMetadataReady(AVFormatContext *context) {
    int err;
    AVDictionary **vPropList = NULL, **aPropList = NULL, **sPropList = NULL;     // TODO also for attachments and chapters data
    AVDictionary *generalProperties = NULL;
    AVDictionaryEntry *t = NULL;

    // Get the properties general container and all streams
    av_dict_set_int(&generalProperties, "type", -1, 0);
    av_dict_set(&generalProperties, "typename", "general", 0);
    av_dict_set_int(&generalProperties, "Bit rate", context->bit_rate, 0);
    av_dict_set_int(&generalProperties, "Duration", context->duration, 0);
    av_dict_set(&generalProperties, "Format", context->iformat->name, 0);
    while ((t = av_dict_get(context->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        av_dict_set(&generalProperties, t->key, t->value, 0);
    }
    if (mShowVideo) {
        vPropList = mVideoStream->getProperties(&err);
        if (err) {
            av_dict_free(&generalProperties);
            return error(err, "Cannot get property list of video stream");;
        }
    }
    if (mAudioStream) {
        aPropList = mAudioStream->getProperties(&err);
        if (err) {
            av_dict_free(&generalProperties);
            if (vPropList) {
                freeAVDictionaryList(vPropList, mVideoStream->getNumOfStreams());
            }
            return error(err, "Cannot get property list of audio stream");;
        }
    }
    if (mSubtitleStream) {
        sPropList = mSubtitleStream->getProperties(&err);
        if (err) {
            if (vPropList) {
                freeAVDictionaryList(vPropList, mVideoStream->getNumOfStreams());
            }
            if (aPropList) {
                freeAVDictionaryList(aPropList, mAudioStream->getNumOfStreams());
            }
            av_dict_free(&generalProperties);
            return error(err, "Cannot get property list of subtitles stream");;
        }
    }

    // Send the data to the callback
    mCallback->onMetadataReady(generalProperties,
                               vPropList, mVideoStream ? mVideoStream->getNumOfStreams() : 0,
                               aPropList, mAudioStream ? mAudioStream->getNumOfStreams() : 0,
                               sPropList, mSubtitleStream ? mSubtitleStream->getNumOfStreams() : 0);

    if (vPropList) {
        freeAVDictionaryList(vPropList, mVideoStream->getNumOfStreams());
    }
    if (aPropList) {
        freeAVDictionaryList(aPropList, mAudioStream->getNumOfStreams());
    }
    if (sPropList) {
        freeAVDictionaryList(sPropList, mSubtitleStream->getNumOfStreams());
    }
    return 0;
}

void Player::sleepMs(long ms) {
    std::this_thread::sleep_for((std::chrono::milliseconds(ms)));
}

