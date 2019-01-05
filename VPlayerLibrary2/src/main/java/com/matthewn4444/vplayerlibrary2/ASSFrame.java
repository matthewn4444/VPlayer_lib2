package com.matthewn4444.vplayerlibrary2;

import android.support.annotation.Nullable;

public class ASSFrame {

    public final long time;
    @Nullable public final ASSBitmap[] images;

    public ASSFrame(long timeMs, @Nullable ASSBitmap[] images) {
        time = timeMs;
        this.images = images;
    }
}
