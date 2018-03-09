package com.matthewn4444.vplayerlibrary2;

import java.io.IOException;

public class VPlayerException extends IOException {

    final private int mCode;

    public VPlayerException(String message, int code) {
        super(message);
        mCode = code;
    }

    public int getErrorCode() {
        return mCode;
    }
}
