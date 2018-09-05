#include "StreamComponent.h"

#define MIN_FRAMES 25
#define MIN_CPU_COUNT 2

const char *sTag = "StreamComponent";

#define _log(...) __android_log_print(ANDROID_LOG_INFO, "VPlayer2Native", __VA_ARGS__);

StreamComponent::StreamComponent(AVFormatContext *context, enum AVMediaType type,
                                 AVPacket *flushPkt, ICallback *callback) :
        mFContext(context),
        mCContext(NULL),
        mType(type),
        mStreamIndex(-1),
        mRequestEnd(false),
        mFinished(0),
        mPktPending(false),
        mPlayerCallback(NULL),
        mCallback(callback),
        mDecodingThread(NULL),
        mPktSerial(-1),
        mPacketQueue(NULL),
        mFlushPkt(flushPkt),
        mIsRealTime(false) {

    // Get the indexes that contain this type of stream
    for (int i = 0; i < context->nb_streams; ++i) {
        if (context->streams[i]->codecpar->codec_type == mType) {
            mAvailStreamIndexes.push_back(i);
        }
    }
}

StreamComponent::~StreamComponent() {
    close();
    mAvailStreamIndexes.clear();
}

void StreamComponent::abort() {
    if (mPacketQueue) {
        mPacketQueue->abort();
    }
}

int StreamComponent::pickBest(int relativeStream) {
    int index = av_find_best_stream(mFContext, mType, -1, relativeStream, NULL, 0);
    if (index == -1) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "No stream is available to pick from [%s]",
                            av_get_media_type_string(mType));
    } else if (index != mStreamIndex) {
        mStreamIndex = index;
        return open();
    }
    return mStreamIndex >= 0 ? 0 : AVERROR_STREAM_NOT_FOUND;
}

int StreamComponent::pickByIndex(int streamNumber, bool fromAllStreams) {
    int index = -1;
    if (fromAllStreams) {
        // Choose index from all streams
        if (mFContext->nb_streams > streamNumber
            && mFContext->streams[streamNumber]->codecpar->codec_type == mType) {
            index = streamNumber;
        }
    } else if (streamNumber < mAvailStreamIndexes.size()) {
        // Choose index relative to all the type of stream
        index = mAvailStreamIndexes[streamNumber];
    }
    if (index == -1) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Cannot choose stream with index %d [%s]",
                            streamNumber, typeName());
    } else if (index != mStreamIndex) {
        mStreamIndex = index;
        return open();
    }
    return mStreamIndex >= 0 ? 0 : AVERROR_STREAM_NOT_FOUND;
}

void StreamComponent::startDecoding() {
    if (!hasStartedDecoding()) {
        if (mStreamIndex != -1) {
            mDecodingThread = new std::thread(&StreamComponent::internalProcessThread, this);
        } else {
            __android_log_print(ANDROID_LOG_WARN, sTag, "Cannot start decoding, invalid index");
        }
    }
}

void StreamComponent::setCallback(IPlayerCallback *callback) {
    std::lock_guard<std::mutex> lk(mErrorMutex);
    mPlayerCallback = callback;
}

void StreamComponent::setPaused(bool paused, int pausePlayRet) {
}

AVDictionary **StreamComponent::getProperties(int *ret) {
    if (mAvailStreamIndexes.empty()) {
        return NULL;
    }

    AVCodec *codec;
    AVStream *stream;
    AVCodecContext *cContext;
    int i;
    AVDictionary **propList = (AVDictionary **) av_mallocz_array(mAvailStreamIndexes.size(),
                                                                 sizeof(*propList));
    for (i = 0; i < mAvailStreamIndexes.size(); ++i) {
        cContext = NULL;
        codec = NULL;
        if ((*ret = getCodecInfo(mAvailStreamIndexes[i], &cContext, &codec)) < 0) {
            // Clean up on error: free the previous entries and dictionary list
            for (i = i - 1; i >= 0; --i) {
                av_dict_free(&propList[i]);
            }
            av_freep(&propList);
            return NULL;
        }
        stream = mFContext->streams[mAvailStreamIndexes[i]];
        av_dict_set_int(&propList[i], "type", mType, 0);
        av_dict_set(&propList[i], "typename", typeName(), 0);
        av_dict_set_int(&propList[i], "ID", mAvailStreamIndexes[i], 0);
        propList[i] = getPropertiesOfStream(cContext, stream, codec);
        avcodec_free_context(&cContext);
    }
    return propList;
}

AVDictionary *StreamComponent::getPropertiesOfStream(AVCodecContext *cContext,
                                                     AVStream *stream, AVCodec *codec) {
    AVDictionary *ret = NULL;
    AVDictionaryEntry *t = NULL;

    av_dict_set(&ret, "Codec ID", codec->long_name, 0);
    if (cContext->bits_per_raw_sample) {
        av_dict_set_int(&ret, "Bit depth", cContext->bits_per_raw_sample, 0);
    }
    while ((t = av_dict_get(stream->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        av_dict_set(&ret, t->key, t->value, 0);
    }
    return ret;
}

int StreamComponent::error(int code, const char *message) {
    std::lock_guard<std::mutex> lk(mErrorMutex);
    if (mPlayerCallback) {
        mPlayerCallback->onError(code, sTag, message);
    }
    mCallback->abort();
    return code;
}

int StreamComponent::open() {
    if (mStreamIndex < 0 || mStreamIndex >= mFContext->nb_streams) {
        return -1;
    }
    close();
    int ret;
    AVCodec *codec = NULL;
    mPacketQueue = new PacketQueue(mStreamIndex);
    if ((ret = getCodecInfo(mStreamIndex, &mCContext, &codec)) < 0) {
        return ret;
    }
    mCContext->pkt_timebase = mFContext->streams[mStreamIndex]->time_base;
    mCContext->thread_count = std::max((int) std::thread::hardware_concurrency(), MIN_CPU_COUNT);

    // TODO Enable to see if this helps anything for speedup, non complient as a setting
    // TODO see if we want to use avcodec_open2 options => threads(auto), refcounted_frames(1)
//    cContext->flags2 |= AV_CODEC_FLAG2_FAST;
    if ((ret = avcodec_open2(mCContext, codec, NULL)) < 0) {
        return ret;
    }
    mFContext->streams[mStreamIndex]->discard = AVDISCARD_DEFAULT;
    mIsRealTime = !strcmp(mFContext->iformat->name, "rtp")
           || !strcmp(mFContext->iformat->name, "rtsp")
           || !strcmp(mFContext->iformat->name, "sdp");

    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "%s stream has opened", typeName());

    mPacketQueue->begin(mFlushPkt);
    return 0;
}

void StreamComponent::close() {
    mRequestEnd = true;
    if (mPacketQueue && !hasAborted()) {
        mPacketQueue->abort();
    }
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "Waiting for %s decoding thread to join...",
                        av_get_media_type_string(mType));
    if (mDecodingThread) {
        mDecodingThread->join();
        mDecodingThread = NULL;
    }
    if (mCContext) {
        avcodec_free_context(&mCContext);
        mCContext = NULL;
    }
    if (mPacketQueue) {
        delete mPacketQueue;
        mPacketQueue = NULL;
    }
    if (mPktPending) {
        av_packet_unref(&mPkt);
    }
    __android_log_print(ANDROID_LOG_VERBOSE, sTag, "%s stream has closed", typeName());
    mRequestEnd = false;
    mFinished = 0;
    mPktPending = false;
    mIsRealTime = false;
}

Clock *StreamComponent::getMasterClock() {
    return mCallback->getMasterClock();
}

Clock *StreamComponent::getExternalClock() {
    return mCallback->getExternalClock();
}

bool StreamComponent::isPaused() {
    Clock *clock = getMasterClock();
    return clock == NULL || getMasterClock()->paused;
}

bool StreamComponent::hasAborted() {
    return mPacketQueue == NULL || mPacketQueue->hasAborted();
}

bool StreamComponent::hasStartedDecoding() {
    return mDecodingThread != NULL;
}

int StreamComponent::getCodecInfo(int streamIndex, AVCodecContext **oCContext, AVCodec **oCodec) {
    if (streamIndex < 0 || streamIndex >= mFContext->nb_streams || *oCContext != NULL
        || *oCodec != NULL) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Cannot get stream info with index %d because "
                "invalid or codec not null", streamIndex);
        return -1;
    }
    int ret;
    *oCContext = avcodec_alloc_context3(NULL);
    if (!*oCContext) {
        return AVERROR(ENOMEM);
    }
    if ((ret = avcodec_parameters_to_context(*oCContext,
                                             mFContext->streams[streamIndex]->codecpar)) < 0) {
        avcodec_free_context(oCContext);
        return ret;
    }
    if (!(*oCodec = avcodec_find_decoder((*oCContext)->codec_id))) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Cannot find a codec for id: %d",
                            (*oCContext)->codec_id);
        avcodec_free_context(oCContext);
        return AVERROR(EINVAL);
    }
    return 0;
}

bool StreamComponent::isQueueFull() {
    AVStream *stream = getStream();
    if (stream == NULL || mPacketQueue == NULL) {
        __android_log_print(ANDROID_LOG_WARN, sTag, "Stream %s has not chosen an index yet",
                            typeName());
        return true;
    }
    return hasAborted() || (stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
           || (mPacketQueue->numPackets() > MIN_FRAMES
               && (!mPacketQueue->duration()
                   || av_q2d(stream->time_base) * mPacketQueue->duration() > 1.0));
}

bool StreamComponent::isFinished() {
    return mFinished == mPacketQueue->serial() && !areFramesPending();
}

bool StreamComponent::isRealTime() {
    return mIsRealTime;
}

int StreamComponent::decodeFrame(void *frame) {
    int ret = AVERROR(EAGAIN);
    if (mStreamIndex < 0 || mStreamIndex >= mFContext->nb_streams || mPacketQueue == NULL
            || mCContext == NULL) {
        return hasAborted() ? AVERROR_EXIT : AVERROR_DECODER_NOT_FOUND;
    }

    while (1) {
        AVPacket pktTmp;
        if (mPacketQueue->serial() == mPktSerial) {
            do {
                if (hasAborted()) {
                    return AVERROR_EXIT;
                }
                onReceiveDecodingFrame(frame, &ret);
                if (ret == AVERROR_EOF) {
                    mFinished = mPktSerial;         // TODO simplify mFinished after implement seek
                    avcodec_flush_buffers(mCContext);
                    return 0;
                }
                if (ret >= 0) {
                    return 1;
                }
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (mPacketQueue->numPackets() == 0) {
                mCallback->onQueueEmpty(this);
            }
            if (mPktPending) {
                av_packet_move_ref(&pktTmp, &mPkt);
                mPktPending = false;
            } else if ((ret = mPacketQueue->dequeue(&pktTmp, &mPktSerial, true)) < 0) {
                return ret;
            }
        } while (mPacketQueue->serial() != mPktSerial);

        if (mPkt.data == mFlushPkt->data) {
            avcodec_flush_buffers(mCContext);
            mFinished = 0;
            onDecodeFlushBuffers();
        } else {
            onDecodeFrame(frame, &pktTmp, &ret);
            av_packet_unref(&pktTmp);
        }
    }
}

void StreamComponent::onDecodeFlushBuffers() {
}

void StreamComponent::internalProcessThread() {
    IPlayerCallback::UniqueCallback unCallback(mPlayerCallback);
    if (!hasAborted()) {
        onProcessThread();
    }
}
