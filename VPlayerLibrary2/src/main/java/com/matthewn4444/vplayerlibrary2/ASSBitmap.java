package com.matthewn4444.vplayerlibrary2;

import android.graphics.Bitmap;
import android.graphics.Rect;
import android.support.annotation.Nullable;
import android.util.Base64;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;

public class ASSBitmap {
    public final Rect rect = new Rect();
    public final byte[] data;
    public final int stride;
    public final boolean changed;

    ASSBitmap(int x, int y, int r, int b, @Nullable byte[] data, boolean changed, int stride) {
        this.data = data;
        this.stride = stride;
        this.changed = changed;
        rect.set(x, y, r, b);
    }

    public @Nullable Bitmap createBitmap() {
        if (data == null) {
            return null;
        }
        Bitmap b = Bitmap.createBitmap(rect.width(), rect.height(), Bitmap.Config.ARGB_8888);
        ByteBuffer buffer = ByteBuffer.wrap(data);
        b.copyPixelsFromBuffer(buffer);
        return b;
    }

    public @Nullable byte[] createPng(int quality) {
        Bitmap b = createBitmap();
        if (b == null) {
            return null;
        }
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        b.compress(Bitmap.CompressFormat.PNG, quality, baos);
        b.recycle();
        return baos.toByteArray();
    }

    public @Nullable String createBase64Png(int quality) {
        byte[] bytes = createPng(quality);
        if (bytes == null) {
            return null;
        }
        return Base64.encodeToString(bytes, Base64.DEFAULT);
    }
}
