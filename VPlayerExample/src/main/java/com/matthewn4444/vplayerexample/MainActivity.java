package com.matthewn4444.vplayerexample;

import android.os.Bundle;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.support.v4.content.ContextCompat;
import android.view.View;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.widget.Toast;

import com.matthewn4444.vplayerlibrary2.VPlayerException;
import com.matthewn4444.vplayerlibrary2.VPlayerListener;
import com.matthewn4444.vplayerlibrary2.VPlayerView;

import java.util.Map;

public class MainActivity extends BaseActivity {

    private VPlayerView mVideoView;
    private ImageButton mPausePlay;
    private SeekBar mSeekBar;
    private Button mNextFrameButton;

    private boolean mPausedBeforeLeave;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        mVideoView = findViewById(R.id.video);
        mPausePlay = findViewById(R.id.pauseplay);
        mSeekBar = findViewById(R.id.seekbar);
        mNextFrameButton = findViewById(R.id.next_frame_button);
        mVideoView.setListener(new VPlayerListener() {
            @Override
            public void onMetadataReady(@NonNull Map<String, String>[] metadataList) {
//                log("Metadata ready", metadataList.length);
//                for (int i = 0; i < metadataList.length; i++) {
//                    Map<String, String> m = metadataList[i];
//                    if (m != null) {      // TODO avoid null
//                        log("=================", m.get("typename"));
//                        for (String key : m.keySet()) {
//                            log("->", key, m.get(key));
//                        }
//                    }
//                }

                mSeekBar.setMax((int) mVideoView.getDuration());
                mSeekBar.setEnabled(true);
            }

            @Override
            public void onStreamReady() {
                log("Stream ready, time to play!");
            }

            @Override
            public void onStreamFinished() {
                Toast.makeText(MainActivity.this, "Stream finished", Toast.LENGTH_LONG).show();
            }

            @Override
            public void onProgressChanged(long currentMs, long durationMs) {
                mSeekBar.setProgress((int) currentMs);
            }

            @Override
            public void onPlaybackChanged(boolean isPlaying) {
                if (mPausedBeforeLeave) {
                    mPausePlay.setImageDrawable(ContextCompat.getDrawable(MainActivity.this,
                            isPlaying
                                    ? R.drawable.ic_play_arrow_white_48dp
                                    : R.drawable.ic_pause_white_48dp));
                }
            }

            @Override
            public void onVideoError(@NonNull VPlayerException exception) {
                Toast.makeText(MainActivity.this, exception.getMessage(), Toast.LENGTH_SHORT).show();
            }
        });

        if (hasStoragePermissions()) {
            loadVideo();
        }

        mPausePlay.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mVideoView.isPaused()) {
                    mVideoView.play();
                } else {
                    mVideoView.pause();
                }
            }
        });

        mSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) {
                    mVideoView.seek(progress);
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                mVideoView.seek(seekBar.getProgress());
            }
        });
        mSeekBar.setEnabled(false);

        mNextFrameButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mVideoView.frameStep();
            }
        });

        mVideoView.seek(362049);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!mPausedBeforeLeave) {
            mVideoView.play();
        }
        mPausedBeforeLeave = true;
    }

    @Override
    protected void onPause() {
        mPausedBeforeLeave = mVideoView.isPaused();
        mVideoView.pause();
        super.onPause();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSIONS_REQUEST_READ_WRITE_STORAGE && hasStoragePermissions()) {
            loadVideo();
        }
    }

    private void loadVideo() {
        mVideoView.openFile(Environment.getExternalStorageDirectory() + "/Movies/video.mkv");
    }
}
