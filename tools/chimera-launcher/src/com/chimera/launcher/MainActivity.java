package com.chimera.launcher;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class MainActivity extends Activity {
    private final List<AppEntry> apps = new ArrayList<>();
    private AppAdapter adapter;
    private TextView emptyState;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setStatusBarColor(Color.rgb(10, 20, 19));
        getWindow().setNavigationBarColor(Color.rgb(10, 20, 19));

        adapter = new AppAdapter();
        setContentView(createLayout());
    }

    @Override
    protected void onResume() {
        super.onResume();
        loadApps();
    }

    private View createLayout() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(34), dp(10), dp(34), dp(24));
        root.setBackgroundColor(Color.rgb(10, 20, 19));
        root.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.MATCH_PARENT));

        TextView title = new TextView(this);
        title.setText("CHIMERA");
        title.setTextColor(Color.rgb(226, 247, 240));
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        title.setLetterSpacing(0.14f);
        root.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(32)));

        GridView grid = new GridView(this);
        grid.setNumColumns(GridView.AUTO_FIT);
        grid.setColumnWidth(dp(132));
        grid.setVerticalSpacing(dp(18));
        grid.setHorizontalSpacing(dp(16));
        grid.setStretchMode(GridView.STRETCH_COLUMN_WIDTH);
        grid.setClipToPadding(false);
        grid.setPadding(0, dp(8), 0, 0);
        grid.setSelector(android.R.color.transparent);
        grid.setAdapter(adapter);
        grid.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                launch(apps.get(position));
            }
        });
        root.addView(grid, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        emptyState = new TextView(this);
        emptyState.setText("沒有可啟動的應用程式");
        emptyState.setTextColor(Color.rgb(111, 142, 136));
        emptyState.setTextSize(14);
        emptyState.setGravity(Gravity.CENTER);
        emptyState.setVisibility(View.GONE);
        root.addView(emptyState, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(44)));

        return root;
    }

    private void loadApps() {
        PackageManager pm = getPackageManager();
        List<AppEntry> launchable = queryLaunchableApps(pm);
        List<AppEntry> next = new ArrayList<>();

        addRequiredApp(next, findByPackage(launchable, "com.android.vending"),
                "Google Play", playStoreIntent(), android.R.drawable.sym_def_app_icon);
        addRequiredApp(next, null, "檔案管理", filesAppIntent(pm),
                android.R.drawable.ic_menu_upload);
        addRequiredApp(next, null, "瀏覽器", browserIntent(),
                android.R.drawable.ic_menu_search);
        addRequiredApp(next, null, "設定", new Intent(Settings.ACTION_SETTINGS),
                android.R.drawable.ic_menu_manage);

        Set<String> seenPackages = new HashSet<>();
        for (AppEntry app : next) {
            if (app.packageName != null) seenPackages.add(app.packageName);
        }
        for (AppEntry app : launchable) {
            if (app.packageName == null || seenPackages.contains(app.packageName)) continue;
            if (isPinnedOrSystemJunk(app.packageName)) continue;
            next.add(app);
            seenPackages.add(app.packageName);
        }

        apps.clear();
        apps.addAll(next);
        emptyState.setVisibility(apps.isEmpty() ? View.VISIBLE : View.GONE);
        adapter.notifyDataSetChanged();
    }

    private List<AppEntry> queryLaunchableApps(PackageManager pm) {
        Intent main = new Intent(Intent.ACTION_MAIN);
        main.addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> resolved = pm.queryIntentActivities(main, 0);
        List<AppEntry> result = new ArrayList<>();
        for (ResolveInfo info : resolved) {
            if (info.activityInfo == null) continue;
            String packageName = info.activityInfo.packageName;
            if (getPackageName().equals(packageName)) continue;
            if (!isUserInstalled(pm, packageName)) continue;
            Intent launch = new Intent(Intent.ACTION_MAIN);
            launch.addCategory(Intent.CATEGORY_LAUNCHER);
            launch.setClassName(packageName, info.activityInfo.name);
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                    | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
            CharSequence rawLabel = info.loadLabel(pm);
            String label = rawLabel == null ? packageName : rawLabel.toString();
            result.add(new AppEntry(label, launch, info.loadIcon(pm), true, packageName));
        }
        return result;
    }

    private AppEntry findByPackage(List<AppEntry> apps, String packageName) {
        for (AppEntry app : apps) {
            if (packageName.equals(app.packageName)) return app;
        }
        return null;
    }

    private boolean isUserInstalled(PackageManager pm, String packageName) {
        try {
            ApplicationInfo app = pm.getApplicationInfo(packageName, 0);
            return (app.flags & (ApplicationInfo.FLAG_SYSTEM
                    | ApplicationInfo.FLAG_UPDATED_SYSTEM_APP)) == 0;
        } catch (PackageManager.NameNotFoundException ignored) {
            return false;
        }
    }

    private boolean isPinnedOrSystemJunk(String packageName) {
        return packageName.equals("com.android.vending")
                || packageName.equals("me.zhanghai.android.files")
                || packageName.equals("com.android.chrome")
                || packageName.equals("com.android.settings")
                || packageName.equals("com.google.android.documentsui")
                || packageName.startsWith("com.tmobile");
    }

    private Intent playStoreIntent() {
        Intent launch = getPackageManager().getLaunchIntentForPackage("com.android.vending");
        if (launch != null) return launch;
        return new Intent(Intent.ACTION_VIEW,
                Uri.parse("market://details?id=com.chimera.launcher"));
    }

    private Intent filesAppIntent(PackageManager pm) {
        Intent explicit = new Intent(Intent.ACTION_MAIN);
        explicit.addCategory(Intent.CATEGORY_LAUNCHER);
        explicit.setClassName("me.zhanghai.android.files",
                "me.zhanghai.android.files.filelist.FileListActivity");
        try {
            pm.getPackageInfo("me.zhanghai.android.files", 0);
            return explicit;
        } catch (PackageManager.NameNotFoundException ignored) {
            return new Intent(this, FileManagerActivity.class);
        }
    }

    private Intent browserIntent() {
        Intent chrome = new Intent(Intent.ACTION_MAIN);
        chrome.addCategory(Intent.CATEGORY_LAUNCHER);
        chrome.setClassName("com.android.chrome", "com.google.android.apps.chrome.Main");
        try {
            getPackageManager().getPackageInfo("com.android.chrome", 0);
            return chrome;
        } catch (PackageManager.NameNotFoundException ignored) {
            return new Intent(this, BrowserActivity.class);
        }
    }

    private Intent packageIntent(PackageManager pm, String packageName, Intent fallback) {
        Intent launch = pm.getLaunchIntentForPackage(packageName);
        if (launch != null) return launch;

        Intent main = new Intent(Intent.ACTION_MAIN);
        main.addCategory(Intent.CATEGORY_LAUNCHER);
        main.setPackage(packageName);
        ResolveInfo info = pm.resolveActivity(main, 0);
        if (info != null && info.activityInfo != null) {
            Intent intent = new Intent(Intent.ACTION_MAIN);
            intent.addCategory(Intent.CATEGORY_LAUNCHER);
            intent.setClassName(info.activityInfo.packageName, info.activityInfo.name);
            return intent;
        }
        return fallback;
    }

    private void addRequiredApp(List<AppEntry> target, AppEntry launchableApp, String label,
                                Intent fallbackIntent, int fallbackIconRes) {
        if (launchableApp != null) {
            target.add(new AppEntry(label, launchableApp.launchIntent, launchableApp.icon,
                    true, launchableApp.packageName));
            return;
        }

        PackageManager pm = getPackageManager();
        Intent intent = fallbackIntent;
        ResolveInfo info = pm.resolveActivity(intent, PackageManager.MATCH_DEFAULT_ONLY);
        boolean explicitAvailable = false;
        if (info == null && intent.getComponent() != null) {
            try {
                pm.getPackageInfo(intent.getComponent().getPackageName(), 0);
                explicitAvailable = true;
            } catch (PackageManager.NameNotFoundException ignored) {
                explicitAvailable = false;
            }
        }
        Drawable icon = info != null && info.activityInfo != null
                ? info.loadIcon(pm)
                : getDrawable(fallbackIconRes);
        Intent launchIntent = info != null || explicitAvailable ? new Intent(intent) : null;
        if (launchIntent != null) {
            launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                    | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        }
        String packageName = intent.getComponent() != null ? intent.getComponent().getPackageName() : null;
        target.add(new AppEntry(label, launchIntent, icon, launchIntent != null, packageName));
    }

    private void launch(AppEntry app) {
        Log.i("ChimeraLauncher", "launch " + app.label + " enabled=" + app.enabled
                + " package=" + app.packageName + " intent=" + app.launchIntent);
        if (!app.enabled || app.launchIntent == null) {
            Toast.makeText(this, app.label + " 尚未安裝", Toast.LENGTH_SHORT).show();
            return;
        }
        try {
            startActivity(app.launchIntent);
        } catch (RuntimeException ex) {
            Log.e("ChimeraLauncher", "launch failed " + app.label, ex);
            Toast.makeText(this, app.label + " 無法開啟", Toast.LENGTH_SHORT).show();
        }
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private final class AppAdapter extends BaseAdapter {
        @Override
        public int getCount() { return apps.size(); }

        @Override
        public Object getItem(int position) { return apps.get(position); }

        @Override
        public long getItemId(int position) { return position; }

        @Override
        public View getView(int position, View convertView, android.view.ViewGroup parent) {
            LinearLayout item = convertView instanceof LinearLayout
                    ? (LinearLayout) convertView
                    : createItemView();
            AppEntry app = apps.get(position);
            ImageView icon = (ImageView) item.getChildAt(0);
            TextView label = (TextView) item.getChildAt(1);
            icon.setImageDrawable(app.icon);
            icon.setAlpha(app.enabled ? 1.0f : 0.38f);
            label.setText(app.label);
            label.setTextColor(app.enabled
                    ? Color.rgb(218, 235, 230)
                    : Color.rgb(111, 142, 136));
            item.setAlpha(app.enabled ? 1.0f : 0.72f);
            item.setContentDescription(app.label + "|" + app.enabled + "|" + app.packageName);
            item.setClickable(true);
            item.setFocusable(true);
            item.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View view) {
                    launch(app);
                }
            });
            icon.setClickable(false);
            icon.setFocusable(false);
            label.setClickable(false);
            label.setFocusable(false);
            return item;
        }

        private LinearLayout createItemView() {
            LinearLayout item = new LinearLayout(MainActivity.this);
            item.setOrientation(LinearLayout.VERTICAL);
            item.setGravity(Gravity.CENTER);
            item.setPadding(dp(10), dp(10), dp(10), dp(8));

            ImageView icon = new ImageView(MainActivity.this);
            item.addView(icon, new LinearLayout.LayoutParams(dp(54), dp(54)));

            TextView label = new TextView(MainActivity.this);
            label.setTextColor(Color.rgb(218, 235, 230));
            label.setTextSize(13);
            label.setGravity(Gravity.CENTER);
            label.setSingleLine(false);
            label.setMaxLines(2);
            LinearLayout.LayoutParams labelParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(42));
            labelParams.topMargin = dp(8);
            item.addView(label, labelParams);
            return item;
        }
    }

    private static final class AppEntry {
        final String label;
        final Intent launchIntent;
        final Drawable icon;
        final boolean enabled;
        final String packageName;

        AppEntry(String label, Intent launchIntent, Drawable icon, boolean enabled, String packageName) {
            this.label = label;
            this.launchIntent = launchIntent;
            this.icon = icon;
            this.enabled = enabled;
            this.packageName = packageName;
        }
    }
}
