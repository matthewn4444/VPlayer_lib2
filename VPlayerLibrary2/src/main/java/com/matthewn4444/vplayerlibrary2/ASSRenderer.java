package com.matthewn4444.vplayerlibrary2;

import android.support.annotation.Nullable;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import static com.matthewn4444.vplayerlibrary2.ASSTrack.SRT_HEADER;
import static com.matthewn4444.vplayerlibrary2.ASSTrack.TrackType;

public class ASSRenderer {

    static {
        System.loadLibrary("ffmpeg");
        System.loadLibrary("application");
        nativeInit();
    }

    static final String DEFAULT_FONT_DROID_PATH = "/system/fonts/DroidSans-Bold.ttf";
    static final String DEFAULT_FONT_DROID_NAME = "Droid Sans Bold";
    static final String DEFAULT_FONT_NOTO_PATH = "/system/fonts/NotoSansCJK-Regular.ttc";
    static final String DEFAULT_FONT_NOTO_NAME = "Noto Sans";

    @SuppressWarnings("unused") // Constant for JNI renderer pointer
    private long mRendererInstance;

    private final List<ASSTrack> mTracks = new ArrayList<>();

    public ASSRenderer() {
        if (initRenderer()) {
            if (new File(DEFAULT_FONT_NOTO_PATH).exists()) {
                setDefaultFont(DEFAULT_FONT_NOTO_PATH, DEFAULT_FONT_NOTO_NAME);
            } else {
                setDefaultFont(DEFAULT_FONT_DROID_PATH, DEFAULT_FONT_DROID_NAME);
            }
        }
    }

    @Nullable
    public ASSTrack createTrack(@Nullable String data) {
        long ptr = nativeCreateTrack(data != null ? data : SRT_HEADER);
        if (ptr != 0) {
            @TrackType int type = data != null && data.trim().startsWith(ASSTrack.ASS_HEADER)
                    ? ASSTrack.ASS : ASSTrack.SRT;
            ASSTrack track = new ASSTrack(ptr, type);
            mTracks.add(track);
            return track;
        }
        return null;
    }

    public void release() {
        nativeRelease();
        for (ASSTrack track: mTracks) {
            track.release();
        }
    }

    @Nullable
    public ASSFrame getImage(long timeMs, ASSTrack track) {
        return nativeGetImage(timeMs, track.getInstance());
    }

    public native void addFont(String name, byte[] fontData);

    public native void setDefaultFont(String fontPath, String fontFamilyName);

    public native void setSize(int width, int height);

    private native void nativeRelease();

    private static native void nativeInit();

    private native boolean initRenderer();

    private native long nativeCreateTrack(String data);

    private synchronized native ASSFrame nativeGetImage(long timeMs, long trackPtr);
}
