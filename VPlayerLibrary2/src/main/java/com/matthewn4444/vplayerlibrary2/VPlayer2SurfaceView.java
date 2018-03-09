package com.matthewn4444.vplayerlibrary2;

import android.content.Context;
import android.graphics.PixelFormat;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

class VPlayer2SurfaceView extends SurfaceView implements SurfaceHolder.Callback {

    protected VPlayer2NativeController mController;
    private boolean mCreated = false;

    public VPlayer2SurfaceView(Context context) {
        this(context, null);
    }

    public VPlayer2SurfaceView(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public VPlayer2SurfaceView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        SurfaceHolder holder = getHolder();
        holder.setFormat(PixelFormat.RGBA_8888);
        holder.addCallback(this);
        mController = new VPlayer2NativeController();       // TODO refactor
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (mController == null) {
            mController = new VPlayer2NativeController();
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        mController.onDestroy();
        mController = null;
        super.onDetachedFromWindow();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width,
                               int height) {
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        if (mCreated) {
            surfaceDestroyed(holder);
        }

        Surface surface = holder.getSurface();
        mController.surfaceCreated(surface);
        mCreated = true;
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        mController.surfaceDestroyed();
        mCreated = false;
    }
}
