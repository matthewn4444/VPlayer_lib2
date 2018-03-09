package com.matthewn4444.vplayerlibrary2;

import android.content.Context;
import android.support.annotation.IntDef;
import android.util.AttributeSet;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

public class VPlayerView extends VPlayer2SurfaceView {


    @Retention(RetentionPolicy.SOURCE)
    @IntDef({AVMEDIA_TYPE_CONTAINER, AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, AVMEDIA_TYPE_SUBTITLE})
    public @interface AVMediaType {}
    public static final int AVMEDIA_TYPE_CONTAINER = -1;
    public static final int AVMEDIA_TYPE_VIDEO = 0;
    public static final int AVMEDIA_TYPE_AUDIO = 1;
    public static final int AVMEDIA_TYPE_SUBTITLE = 2;

    public VPlayerView(Context context) {
        this(context, null);
    }

    public VPlayerView(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public VPlayerView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
    }

    public void openFile(String filepath) {
        mController.open(filepath);
    }

    public void setListener(VPlayerListener listener) {
        mController.setListener(listener);
    }
}
