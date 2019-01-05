package com.matthewn4444.vplayerlibrary2;

import android.support.annotation.IntDef;
import android.util.Log;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

public class ASSTrack {
    private static final String TAG = "ASSTrack";
    private static final String SRT_ARROW = " --> ";
    static final String ASS_HEADER = "[Script Info]";
    static final String SRT_HEADER =
            ASS_HEADER + "\n" +
            "Title:\n" +
            "ScriptType: v4.00+\n" +
            "WrapStyle: 0\n" +
            "PlayResX: 1280\n" +
            "PlayResY: 720\n" +
            "ScaledBorderAndShadow: yes\n" +
            "\n" +
            "[V4 Styles]\n" +
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, TertiaryColour, " +
                    "BackColour, Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, " +
                    "MarginR, MarginV, AlphaLevel, Encoding\n" +
            "Style: Default, Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000," +
                    "-1,0,1,1,2,2,30,30,30,0,0\n" +
            "[Events]\n" +
            "Format: Start, End, Style, Text\n";

    @Retention(RetentionPolicy.SOURCE)
    @IntDef({ SRT, ASS })
    public @interface TrackType {}
    public static final int ASS = 0;
    public static final int SRT = 1;

    private long mInstance;

    public final long type;

    ASSTrack(long instance, @TrackType int type) {
        mInstance = instance;
        this.type = type;
    }

    long getInstance() {
        return mInstance;
    }

    public void addData(String subtitleData) {
        if (mInstance > 0) {
            if (type == SRT) {
                subtitleData = convertSrtToAss(subtitleData);
                VPlayer2NativeController.log("<<<", subtitleData);
            }
            nativeAddData(mInstance, subtitleData);
        } else {
            Log.w(TAG, "Cannot add data because track is not initialized");
        }
    }

    public void flush() {
        if (mInstance > 0) {
            nativeFlush(mInstance);
        } else {
            Log.w(TAG, "Cannot flush because track is not initialized");
        }
    }

    void release() {
        if (mInstance > 0) {
            nativeRelease(mInstance);
            mInstance = 0;
        } else {
            Log.e(TAG, "Cannot release track because not initialized");
        }
    }

    private String convertSrtToAss(String subtitleData) {
        String[] lines = subtitleData.split("\n");

        // Incase there is no number at the first line
        int timeLineOffset = lines[0].contains(SRT_ARROW) ? 0 : 1;
        String[] times = lines[timeLineOffset].replaceAll(",", ".").split(SRT_ARROW);
        StringBuilder data = new StringBuilder("Dialogue: " + times[0] + "," + times[1]
                + ",Default,");
        for (int i = timeLineOffset + 1; i < lines.length; i++) {
            data.append(lines[i]).append("\\N");
        }
        return data.toString();
    }

    private native void nativeAddData(long ptr, String data);

    private native void nativeFlush(long ptr);

    private native void nativeRelease(long ptr);
}
