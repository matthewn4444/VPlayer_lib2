package com.matthewn4444.vplayerlibrary2;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.AsyncTask;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.support.annotation.MainThread;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Surface;

import java.util.Map;

public class VPlayer2NativeController {
    private static final int[] sAudioChannels = {
            // No Channels
            AudioFormat.CHANNEL_OUT_STEREO,
            // 1 Channel
            AudioFormat.CHANNEL_OUT_MONO,
            // 2 Channels
            AudioFormat.CHANNEL_OUT_STEREO,
            // 3 Channels
            AudioFormat.CHANNEL_OUT_STEREO | AudioFormat.CHANNEL_OUT_FRONT_CENTER,
            // 4 Channels
            AudioFormat.CHANNEL_OUT_QUAD,
            // 5 Channels
            AudioFormat.CHANNEL_OUT_QUAD | AudioFormat.CHANNEL_OUT_FRONT_CENTER,
            // 6 Channels
            AudioFormat.CHANNEL_OUT_5POINT1,
            // 7 Channels
            AudioFormat.CHANNEL_OUT_5POINT1 | AudioFormat.CHANNEL_OUT_BACK_CENTER,
            // 8 Channels
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                    ? AudioFormat.CHANNEL_OUT_7POINT1_SURROUND : AudioFormat.CHANNEL_OUT_7POINT1
    };

    private static final long SEND_SUBTITLE_FRAME_SIZE_TIMEOUT = 300;

    static {
        // TODO allow dynamic path loading, check for text relocations for armv7
        System.loadLibrary("ffmpeg");
        System.loadLibrary("application");
        nativeInit();
    }

    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    @SuppressWarnings("unused") // Constant for JNI player pointer
    private long mNativePlayerInstance;

    @SuppressWarnings("unused") // Constant for JNI handler
    private long mNativeJniHandlerInstance;

    @SuppressWarnings("unused") // Constant for JNI video renderer
    private long mNativeJniVideoRendererInstance;

    private VPlayerListener mListener;
    private boolean mStreamReady;
    private boolean mHasInitError;

    // Sending subtitles to player, but delay sending to avoid flooding
    private int mPendingSubtitleWidth;
    private int mPendingSubtitleHeight;
    private long mLastTimeSentSubtitleSize;
    private boolean mWaitingToSendSubtitleSize;

    private final Runnable mSendSubtitleFrameSize = new Runnable() {
        @Override
        public void run() {
            mWaitingToSendSubtitleSize = false;
            mLastTimeSentSubtitleSize = SystemClock.currentThreadTimeMillis();
            setSubtitleFrameSize(mPendingSubtitleWidth, mPendingSubtitleHeight);
        }
    };

    VPlayer2NativeController(int displayWidth, int displayHeight) {
        if (initPlayer()) {
            setSubtitleFrameSize(displayWidth, displayHeight);
        } else {
            mHasInitError = true;
        }
    }

    public void setListener(VPlayerListener listener) {
        mListener = listener;

        // There was a jni error on init in constructor
        if (mHasInitError && listener != null) {
            if (Looper.myLooper() == Looper.getMainLooper()) {
                mListener.onVideoError(new VPlayerException(
                        "Cannot play because of internal error", -1));
            } else {
                mMainHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        mListener.onVideoError(new VPlayerException(
                                "Cannot play because of internal error", -1));
                    }
                });
            }
        }
        mHasInitError = false;
    }

    public void open(final String streamUrlOrFileName) {
        nativeOpen(streamUrlOrFileName);
    }

    public boolean isStreamReady() {
        return mStreamReady;
    }

    public void onDestroy() {
        AsyncTask.execute(new Runnable() {
            @Override
            public void run() {
                destroyPlayer();
            }
        });
    }

    @MainThread
    public void internalSetSubtitleFrameSize(int width, int height) {
        mPendingSubtitleWidth = width;
        mPendingSubtitleHeight = height;

        // Avoid flooding the player with adjusting its frame size by waiting after last input
        if (SystemClock.currentThreadTimeMillis() - mLastTimeSentSubtitleSize
                > SEND_SUBTITLE_FRAME_SIZE_TIMEOUT) {
            mMainHandler.removeCallbacks(mSendSubtitleFrameSize);
            mSendSubtitleFrameSize.run();
        } else if (!mWaitingToSendSubtitleSize) {
            mMainHandler.postDelayed(mSendSubtitleFrameSize, SEND_SUBTITLE_FRAME_SIZE_TIMEOUT);
            mWaitingToSendSubtitleSize = true;
        }
    }

    // Called from jni
    private void nativeMetadataReady(final Map<String, String>[] data) {
        if (mListener != null) {
            mMainHandler.post(new Runnable() {
                @Override
                public void run() {
                    mListener.onMetadataReady(data);
                }
            });
        }
    }

    private void nativeStreamError(final int errorCode, final String message) {
        if (mListener != null) {
            mMainHandler.post(new Runnable() {
                @Override
                public void run() {
                    mListener.onVideoError(new VPlayerException(message, errorCode));
                }
            });
        }
    }

    private void nativeStreamReady() {
        mStreamReady = true;
        if (mListener != null) {
            mMainHandler.post(new Runnable() {
                @Override
                public void run() {
                    mListener.onStreamReady();
                }
            });
        }
    }

    private void nativeStreamFinished() {
        if (mListener != null) {
            mMainHandler.post(new Runnable() {
                @Override
                public void run() {
                    mListener.onStreamFinished();
                }
            });
        }
    }

    private AudioTrack nativeCreateAudioTrack(int sampleRateHz, int numOfChannels) {
        for (;;) {
            int channelConfig = numOfChannels < sAudioChannels.length
                    ? sAudioChannels[numOfChannels] : AudioFormat.CHANNEL_OUT_STEREO;
            try {
                int minBufferSize = AudioTrack.getMinBufferSize(sampleRateHz,
                        channelConfig, AudioFormat.ENCODING_PCM_16BIT);
                return new AudioTrack(
                        new AudioAttributes.Builder()
                                .setUsage(AudioAttributes.USAGE_MEDIA)
                                .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
                                .build(),
                        new AudioFormat.Builder()
                                .setSampleRate(sampleRateHz)
                                .setChannelMask(channelConfig)
                                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .build(),
                        minBufferSize, AudioTrack.MODE_STREAM,
                        AudioManager.AUDIO_SESSION_ID_GENERATE);
            } catch (IllegalArgumentException e) {
                // Back off number of channels if failed
                if (numOfChannels > 2) {
                    numOfChannels = 2;
                } else if (numOfChannels > 1) {
                    numOfChannels = 1;
                } else {
                    throw e;
                }
            }
        }
    }

    /**
     *  TODO support multiple screen sizes, wide, aspect etc for surface view resizing
     // Determine the video and width of the video
     //        AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
     //        AVCodecParameters *codecpar = st->codecpar;
     //        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
     //        if (codecpar->width)
     //            set_default_window_size(codecpar->width, codecpar->height, sar);
     */

    private static native void nativeInit();

    private native boolean initPlayer();

    private native void destroyPlayer();

    private native boolean nativeOpen(String streamFileUrl);

    private native void setSubtitleFrameSize(int width, int height);

    public native void surfaceCreated(@NonNull Surface videoSurface, @Nullable Surface subSurface);

    native void surfaceDestroyed();

    protected static void log(Object... txt) {
        String returnStr = "";
        int i = 1;
        int size = txt.length;
        if (size != 0) {
            returnStr = txt[0] == null ? "null" : txt[0].toString();
            for (; i < size; i++) {
                returnStr += ", "
                        + (txt[i] == null ? "null" : txt[i].toString());
            }
        }
        Log.i("lunch", returnStr);
    }
}
