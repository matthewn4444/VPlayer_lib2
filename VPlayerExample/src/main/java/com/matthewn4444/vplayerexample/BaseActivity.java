package com.matthewn4444.vplayerexample;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;

public abstract class BaseActivity extends AppCompatActivity {
    protected static final int PERMISSIONS_REQUEST_READ_WRITE_STORAGE = 1;

    protected static void requestStoragePermissions(Activity activity) {
        ActivityCompat.requestPermissions(activity,
                new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE},
                PERMISSIONS_REQUEST_READ_WRITE_STORAGE);
    }


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        verifyStoragePermissionsOrShowDialogs();
    }

    protected boolean verifyStoragePermissionsOrShowDialogs() {
        if (!hasStoragePermissions()) {

            // Should we show an explanation?
            if (shouldShowRequestPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                    || shouldShowRequestPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
                new AlertDialog.Builder(this)
                        .setMessage("Require read access to view videos")
                        .setNeutralButton(android.R.string.ok, new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                                ActivityCompat.requestPermissions(BaseActivity.this,
                                        new String[]{Manifest.permission.READ_EXTERNAL_STORAGE},
                                        PERMISSIONS_REQUEST_READ_WRITE_STORAGE);
                            }
                        })
                        .show();
            } else {
                requestStoragePermissions(this);
            }
            return false;
        }
        return true;
    }

    protected boolean hasStoragePermissions() {
        return checkPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                && checkPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE);
    }

    protected boolean checkPermission(String permission) {
        return ContextCompat.checkSelfPermission(this, permission)
                == PackageManager.PERMISSION_GRANTED;
    }

    protected boolean shouldShowRequestPermission(String permission) {
        return ActivityCompat.shouldShowRequestPermissionRationale(this, permission);
    }

    protected static void log(Object... txt) {
        String returnStr = "";
        int i = 1;
        int size = txt.length;
        if (size != 0) {
            returnStr = txt[0] == null ? "null" : txt[0].toString();
            for (; i < size; i++) {
                returnStr += ", "
                        + (txt[i] == null ? "null" : txt[i].toString());
            }
        }
        Log.i("lunch", returnStr);
    }
}
