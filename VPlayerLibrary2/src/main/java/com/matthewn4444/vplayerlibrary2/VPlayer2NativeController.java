package com.matthewn4444.vplayerlibrary2;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRouting;
import android.media.AudioTrack;
import android.os.AsyncTask;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.support.annotation.MainThread;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.util.Log;
import android.view.Surface;

import java.io.File;
import java.util.Map;

import static com.matthewn4444.vplayerlibrary2.ASSRenderer.DEFAULT_FONT_DROID_NAME;
import static com.matthewn4444.vplayerlibrary2.ASSRenderer.DEFAULT_FONT_DROID_PATH;
import static com.matthewn4444.vplayerlibrary2.ASSRenderer.DEFAULT_FONT_NOTO_NAME;
import static com.matthewn4444.vplayerlibrary2.ASSRenderer.DEFAULT_FONT_NOTO_PATH;

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

    private static final boolean AT_LEAST_N = Build.VERSION.SDK_INT >= Build.VERSION_CODES.N;
    private static final long SEND_SUBTITLE_FRAME_SIZE_TIMEOUT = 300;

    static {
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

    private AudioTrack mAudioTrack;

    private VPlayerListener mListener;
    private boolean mStreamReady;
    private boolean mHasInitError;

    // Sending subtitles to player, but delay sending to avoid flooding
    int mPendingSubtitleWidth;
    int mPendingSubtitleHeight;
    private long mLastTimeSentSubtitleSize;
    private boolean mWaitingToSendSubtitleSize;

    private final Runnable mSendSubtitleFrameSize = new Runnable() {
        @Override
        public void run() {
            mWaitingToSendSubtitleSize = false;
            mLastTimeSentSubtitleSize = SystemClock.currentThreadTimeMillis();
            nativeSetSubtitleFrameSize(mPendingSubtitleWidth, mPendingSubtitleHeight);
        }
    };

    private final AudioRouting.OnRoutingChangedListener mRoutingChangedListener;

    VPlayer2NativeController() {
        if (initPlayer()) {

            // Check for default font
            if (new File(DEFAULT_FONT_NOTO_PATH).exists()) {
                nativeSetDefaultSubtitleFont(DEFAULT_FONT_NOTO_PATH, DEFAULT_FONT_NOTO_NAME);
            } else {
                nativeSetDefaultSubtitleFont(DEFAULT_FONT_DROID_PATH, DEFAULT_FONT_DROID_NAME);
            }
        } else {
            mHasInitError = true;
        }

        if (AT_LEAST_N) {
            mRoutingChangedListener = new AudioRouting.OnRoutingChangedListener() {
                @Override
                public void onRoutingChanged(AudioRouting router) {
                    remeasureAudioLatency();
                }
            };
        } else {
            mRoutingChangedListener = null;
        }
    }

    void setListener(VPlayerListener listener) {
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

    void onDestroy() {
        if (AT_LEAST_N && mAudioTrack != null) {
            mAudioTrack.removeOnRoutingChangedListener(mRoutingChangedListener);
            mAudioTrack = null;
        }
        AsyncTask.execute(new Runnable() {
            @Override
            public void run() {
                destroyPlayer();
            }
        });
    }

    @MainThread
    void setSubtitleResolution(int width, int height) {
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

    private void nativeProgressChanged(final long currentMs, final long durationMs) {
        if (mListener != null) {
            mMainHandler.post(new Runnable() {
                @Override
                public void run() {
                    mListener.onProgressChanged(currentMs, durationMs);
                }
            });
        }
    }

    private void nativePlaybackChanged(final boolean isPlaying) {
        if (mListener != null) {
            mMainHandler.post(new Runnable() {
                @Override
                public void run() {
                    mListener.onPlaybackChanged(isPlaying);
                }
            });
        }
    }

    private AudioTrack nativeCreateAudioTrack(int sampleRateHz, int numOfChannels) {
        if (AT_LEAST_N && mAudioTrack != null) {
            mAudioTrack.removeOnRoutingChangedListener(mRoutingChangedListener);
        }
        for (;;) {
            int channelConfig = numOfChannels < sAudioChannels.length
                    ? sAudioChannels[numOfChannels] : AudioFormat.CHANNEL_OUT_STEREO;
            try {
                int minBufferSize = AudioTrack.getMinBufferSize(sampleRateHz,
                        channelConfig, AudioFormat.ENCODING_PCM_16BIT);
                mAudioTrack = new AudioTrack(
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
                if (AT_LEAST_N) {
                    mAudioTrack.addOnRoutingChangedListener(mRoutingChangedListener, mMainHandler);
                }
                return mAudioTrack;
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

    private native void nativeSetSubtitleFrameSize(int width, int height);

    native void nativePlay();

    native void nativePause();

    native void nativeSeek(long positionMill);

    native void nativeFrameStep();

    native void nativeSetDefaultSubtitleFont(String fontPath, String fontFamily);

    native void nativeRenderLastFrame();

    native void remeasureAudioLatency();

    public native void surfaceCreated(@NonNull Surface videoSurface, @Nullable Surface subSurface);

    native void surfaceDestroyed();

    native boolean nativeIsPaused();

    native long nativeGetDurationMill();

    native long nativeGetPlaybackMill();

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
