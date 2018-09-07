package com.matthewn4444.vplayerlibrary2;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.PixelFormat;
import android.os.AsyncTask;
import android.support.annotation.IntDef;
import android.support.annotation.MainThread;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.FrameLayout;

import java.io.File;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

public class VPlayerView extends FrameLayout {
    private static final String TAG = "VPlayerView";

    @Retention(RetentionPolicy.SOURCE)
    @IntDef({AVMEDIA_TYPE_CONTAINER, AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, AVMEDIA_TYPE_SUBTITLE})
    public @interface AVMediaType {}
    public static final int AVMEDIA_TYPE_CONTAINER = -1;
    public static final int AVMEDIA_TYPE_VIDEO = 0;
    public static final int AVMEDIA_TYPE_AUDIO = 1;
    public static final int AVMEDIA_TYPE_SUBTITLE = 2;


    private final SurfaceHolder.Callback mSurfaceCallback = new SurfaceHolder.Callback() {

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            if (mVideoSurface.getHolder() == holder) {
                mVideoSurfaceCreated = true;
            }
            if (mSubtitlesSurface.getHolder() == holder) {
                mSubtitleSurfaceCreated = true;
            }
            if (mVideoSurfaceCreated && mSubtitleSurfaceCreated) {
                mController.surfaceCreated(mVideoSurface.getHolder().getSurface(),
                       mSubtitlesSurface.getHolder().getSurface());
                if (mPlayWhenSurfacesReady) {
                    mPlayWhenSurfacesReady = false;
                    mController.play();
                } else {
                    // When returning back, player needs render frames on new surfaces
                    AsyncTask.execute(new Runnable() {
                        @Override
                        public void run() {
                            mController.nativeRenderLastFrame();
                        }
                    });
                }
            }
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            if (mVideoSurface.getHolder() == holder) {
                mVideoSurfaceCreated = false;
            }
            if (mSubtitlesSurface.getHolder() == holder) {
                mSubtitleSurfaceCreated = false;
            }
            mController.surfaceDestroyed();
        }
    };

    private final SurfaceView mVideoSurface;
    private final SurfaceView mSubtitlesSurface;

    private VPlayer2NativeController mController;

    private boolean mVideoSurfaceCreated;
    private boolean mSubtitleSurfaceCreated;
    private boolean mPlayWhenSurfacesReady;

    public VPlayerView(@NonNull Context context) {
        this(context, null);
    }

    public VPlayerView(@NonNull Context context, @Nullable AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public VPlayerView(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public VPlayerView(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr,
                       int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        mVideoSurface = new SurfaceView(context);
        mSubtitlesSurface = new SurfaceView(context);
        addView(mVideoSurface);
        addView(mSubtitlesSurface);
        mVideoSurface.getHolder().addCallback(mSurfaceCallback);
        mSubtitlesSurface.getHolder().addCallback(mSurfaceCallback);
        mVideoSurface.getHolder().setFormat(PixelFormat.RGBA_8888);
        mSubtitlesSurface.setZOrderMediaOverlay(true);
        mSubtitlesSurface.setZOrderOnTop(true);
        mSubtitlesSurface.getHolder().setFormat(PixelFormat.RGBA_8888);

        final DisplayMetrics dm = Resources.getSystem().getDisplayMetrics();
        mController = new VPlayer2NativeController(dm.widthPixels, dm.heightPixels);
    }

    public void openFile(String filepath) {
        mController.open(filepath);
    }

    public void play() {
        // Play when the surfaces are ready otherwise we will have dropped frames
        if (mVideoSurfaceCreated && mSubtitleSurfaceCreated) {
            mPlayWhenSurfacesReady = false;
            mController.play();
        } else {
            mPlayWhenSurfacesReady = true;
        }
    }

    public void pause() {
        mPlayWhenSurfacesReady = false;
        mController.pause();
    }

    public boolean isPaused() {
        return mController.nativeIsPaused();
    }

    public void setListener(VPlayerListener listener) {
        mController.setListener(listener);
    }

    @MainThread
    public void setSubtitleFrameSize(int width, int height) {
        mController.setSubtitleFrameSize(width, height);
    }

    public void setDefaultSubtitleFont(String fontPath, String fontFamily) {
        if (new File(fontPath).exists()) {
            mController.nativeSetDefaultSubtitleFont(fontPath, fontFamily);
        } else {
            Log.i(TAG, String.format("Subtitle '%s' does not exist on path '%s'", fontFamily,
                    fontPath));
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        mController.onDestroy();
        mController = null;
        super.onDetachedFromWindow();
    }
}
