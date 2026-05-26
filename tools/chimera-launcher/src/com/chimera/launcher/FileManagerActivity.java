package com.chimera.launcher;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

public final class FileManagerActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.rgb(10, 20, 19));
        getWindow().setNavigationBarColor(Color.rgb(10, 20, 19));
        showFallbackSurface();
        openSystemFiles();
    }

    private void openSystemFiles() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivity(intent);
        } catch (ActivityNotFoundException ex) {
            try {
                startActivity(new Intent(Settings.ACTION_INTERNAL_STORAGE_SETTINGS));
            } catch (ActivityNotFoundException ignored) {
                Toast.makeText(this, "檔案管理不可用", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void showFallbackSurface() {
        LinearLayout root = new LinearLayout(this);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(Color.rgb(10, 20, 19));
        TextView label = new TextView(this);
        label.setText("檔案管理");
        label.setTextColor(Color.rgb(226, 247, 240));
        label.setTextSize(22);
        root.addView(label);
        setContentView(root);
    }
}
