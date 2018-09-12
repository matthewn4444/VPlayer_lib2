package com.matthewn4444.vplayerlibrary2;

import android.support.annotation.NonNull;

import java.util.Map;

public interface VPlayerListener {
    public void onMetadataReady(@NonNull Map<String, String>[] metadataList);
    public void onStreamReady();
    public void onStreamFinished();
    public void onProgressChanged(long currentMs, long durationMs);
    public void onPlaybackChanged(boolean isPlaying);
    public void onVideoError(@NonNull VPlayerException exception);
}
