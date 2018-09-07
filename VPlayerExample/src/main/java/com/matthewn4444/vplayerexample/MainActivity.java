package com.matthewn4444.vplayerexample;

import android.os.Bundle;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.view.View;
import android.widget.Toast;

import com.matthewn4444.vplayerlibrary2.VPlayerException;
import com.matthewn4444.vplayerlibrary2.VPlayerListener;
import com.matthewn4444.vplayerlibrary2.VPlayerView;

import java.util.Map;

public class MainActivity extends BaseActivity {

    VPlayerView mView;

    private boolean mPausedBeforeLeave;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mView = new VPlayerView(this);
        setContentView(mView);
        mView.setListener(new VPlayerListener() {
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
            public void onVideoError(@NonNull VPlayerException exception) {
                Toast.makeText(MainActivity.this, exception.getMessage(), Toast.LENGTH_SHORT).show();
            }
        });

        if (hasStoragePermissions()) {
            loadVideo();
        }

        mView.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mView.isPaused()) {
                    mView.play();
                } else {
                    mView.pause();
                }
            }
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!mPausedBeforeLeave) {
            mView.play();
        }
    }

    @Override
    protected void onPause() {
        mPausedBeforeLeave = mView.isPaused();
        mView.pause();
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
//        mView.openFile("https://www.w3schools.com/html/mov_bbb.mp4");
//        mView.openFile("https://media.w3.org/2010/05/sintel/trailer.mp4");
        mView.openFile(Environment.getExternalStorageDirectory() + "/Movies/video.mkv");
    }
}
