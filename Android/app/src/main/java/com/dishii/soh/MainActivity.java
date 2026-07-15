
package com.dishii.soh;
import org.libsdl.app.SDLActivity;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;

import android.provider.Settings;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.FileOutputStream;
import java.io.FileInputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.concurrent.CountDownLatch;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import android.Manifest;
import android.content.pm.PackageManager;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import android.os.Build;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Toast;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import android.util.Log;

import android.view.KeyEvent;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.graphics.Rect;
import android.graphics.Color;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewTreeObserver;
import android.widget.ImageView;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

import java.util.concurrent.Executors;
import android.app.AlertDialog;

import android.view.InputDevice;
import android.os.Vibrator;
import android.os.VibrationEffect;

//This class is the main SDLActivity and just sets up a bunch of default files
public class MainActivity extends SDLActivity{

    SharedPreferences preferences;
    private static final CountDownLatch setupLatch = new CountDownLatch(1);
    private volatile boolean mIsAiming = false;
    private static final int COPY_BUFFER_SIZE = 65536;
    private static final int RUMBLE_MAX_DURATION_MS = 5000;
    private static final String DATA_FOLDER_NAME = "SOHFUSION";
    private static String currentDataRootPath = "/storage/emulated/0/" + DATA_FOLDER_NAME;
    private static final String PREF_DATA_ROOT_PATH = "dataRootPath";
    private static final String PREF_LEGACY_DATA_MIGRATION_COMPLETE = "legacyDataMigrationComplete";
    private static final String PREF_MM_IMPORT_DECISION_COMPLETE = "mmImportDecisionComplete";
    private static final String TWO_SHIP_MM_O2R_PATH = "/storage/emulated/0/2S2H/mm.o2r";
    private static final String PREF_MM_FORM_IMPORT_DECISION_COMPLETE = "mmFormImportDecisionCompleteV1";
    private static final String TWO_SHIP_3DS_FORM_MODELS_PATH =
            "/storage/emulated/0/2S2H/mods/3 - Link 3DS.o2r";
    private static final String TWO_SHIP_3DS_FORM_TEXTURES_PATH =
            "/storage/emulated/0/2S2H/mods/3 - Link 3DS Textures.o2r";
    private static final String TWO_SHIP_3DS_FORM_HD_TEXTURES_PATH =
            "/storage/emulated/0/2S2H/mods/0-3DS HD Link.o2r";
    private static final String IMPORTED_MM_FORM_MODELS_NAME = "sohnei-3ds-form-models.o2r";
    private static final String IMPORTED_MM_FORM_TEXTURES_NAME = "sohnei-3ds-form-textures.o2r";
    private static final Set<String> MM_FORM_OBJECT_NAMES = new HashSet<>(Arrays.asList(
            "object_link_boy", "object_link_goron", "object_link_goron_hands_bottle",
            "object_link_goron_hands_closed", "object_link_goron_hands_open", "object_link_nuts",
            "object_link_zora", "object_link_zora_hands_bottle", "object_link_zora_hands_closed",
            "object_link_zora_hands_open"));
    private static final Set<String> MM_FORM_TEXTURE_EXCLUDED_ENTRIES = new HashSet<>(Arrays.asList(
            "alt/objects/object_link_boy/gLinkFierceDeitySkel_link_demon_f01_ci8",
            "alt/objects/object_link_boy/gLinkFierceDeitySkel_link_demon_f01_pal_rgba16",
            "alt/objects/object_link_goron/Shine32xSoft",
            "alt/objects/object_link_goron/gLinkGoronSkel_tex_004340_i8_png_001_rgba16",
            "alt/objects/object_link_nuts/link_nuts_gakki",
            "alt/objects/object_link_zora/link_zora_guite"));
    private static final Set<String> MM_FORM_TEXTURE_HD_ENTRIES = new HashSet<>(Arrays.asList(
            "alt/objects/object_link_goron/gLinkGoronSkel_link_goron_e00_rgba16",
            "alt/objects/object_link_zora/gLinkZoraSkel_link_zora_3_rgba16",
            "alt/objects/object_link_zora/gLinkZoraSkel_link_zora_4_rgba16",
            "alt/objects/object_link_zora/gLinkZoraSkel_link_zora_5_rgba16",
            "alt/objects/object_link_zora_hands_bottle/gLinkZoraSkel_link_zora_5_rgba16",
            "alt/objects/object_link_zora_hands_closed/gLinkZoraSkel_link_zora_5_rgba16",
            "alt/objects/object_link_zora_hands_open/gLinkZoraSkel_link_zora_5_rgba16"));
    private static final String PREF_MM_ICON_IMPORT_DECISION_COMPLETE = "mmIconImportDecisionCompleteV2";
    private static final String TWO_SHIP_MM_RELOADED_ICON_PACK_PATH =
            "/storage/emulated/0/2S2H/mods/2 - MM_Reloaded_v11.0.2_HD.o2r";
    private static final String TWO_SHIP_3DS_HUD_ICON_PACK_PATH =
            "/storage/emulated/0/2S2H/mods/1 - 3DS HUD Pack.o2r";
    private static final String IMPORTED_MM_ICON_PACK_NAME = "mm-icons.o2r";
    private static final String LEGACY_IMPORTED_MM_ICON_PACK_NAME = "2s2h-3ds-hud-icons.o2r";
    private static final String MM_ICON_ARCHIVE_PREFIX = "alt/icon_item_static_yar/";
    private static final long FULL_HD_MM_ICON_MIN_BYTES = 262000;
    private static final Set<String> MM_MASK_ICON_NAMES = new HashSet<>(Arrays.asList(
            "gItemIconPostmansHatTex", "gItemIconAllNightMaskTex", "gItemIconBlastMaskTex",
            "gItemIconStoneMaskTex", "gItemIconGreatFairyMaskTex", "gItemIconDekuMaskTex",
            "gItemIconKeatonMaskTex", "gItemIconBremenMaskTex", "gItemIconBunnyHoodTex",
            "gItemIconDonGeroMaskTex", "gItemIconMaskOfScentsTex", "gItemIconGoronMaskTex",
            "gItemIconRomaniMaskTex", "gItemIconCircusLeaderMaskTex", "gItemIconKafeisMaskTex",
            "gItemIconCouplesMaskTex", "gItemIconMaskOfTruthTex", "gItemIconZoraMaskTex",
            "gItemIconKamaroMaskTex", "gItemIconGibdoMaskTex", "gItemIconGaroMaskTex",
            "gItemIconCaptainsHatTex", "gItemIconGiantsMaskTex", "gItemIconFierceDeityMaskTex"));
    private static final String PREF_TOUCH_CONTROLS_DISABLED = "touchControlsDisabled";
    // Legacy key name: true means the touch controls are hidden, not visible.
    private static final String PREF_TOUCH_CONTROLS_HIDDEN = "controlsVisible";
    private static final String SUPPORT_FILES_VERSION_MARKER = ".android_support_files_version";
    // Bump this only when bundled Android support assets or archive layout changes.
    private static final String SUPPORT_FILES_VERSION = "sohfusion-android-support-1";
    private AlertDialog dataRootMigrationDialog;
    private AlertDialog setupProgressDialog;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.i("SoH", "onCreate start");

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        preferences = getSharedPreferences("com.linkzenic.sohfusion.prefs", Context.MODE_PRIVATE);
        updateCurrentDataRootPath();

        Log.i("SoH", "hasStoragePermission=" + hasStoragePermission());

        // Check if storage permissions are granted
        if (hasStoragePermission()) {
            beginSetupOrChooseDataRoot();
        } else {
            requestStoragePermission();
        }

        setupControllerOverlay();
        applyImmersiveFullscreen();
        attachController();

        Log.i("SoH", "onCreate complete");
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersiveFullscreen();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveFullscreen();
        }
    }

    private void applyImmersiveFullscreen() {
        Window window = getWindow();
        if (window == null) {
            return;
        }

        View decorView = window.getDecorView();
        int flags = View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
        decorView.setSystemUiVisibility(flags);
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            window.setStatusBarColor(Color.TRANSPARENT);
            window.setNavigationBarColor(Color.TRANSPARENT);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams attributes = window.getAttributes();
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(attributes);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            window.setNavigationBarContrastEnforced(false);
            window.setStatusBarContrastEnforced(false);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    public static void waitForSetupFromNative() {
        try {
            setupLatch.await();  // Block until setup is complete
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    public static String getDataRootPathFromNative() {
        return currentDataRootPath;
    }

    private void doVersionCheck() {
        int currentVersion = BuildConfig.VERSION_CODE;
        int storedVersion = preferences.getInt("appVersion", 1);

        if (!isSupportFilesMarkerCurrent()) {
            deleteOutdatedAssets();
        }

        if (currentVersion > storedVersion) {
            preferences.edit().putInt("appVersion", currentVersion).apply();
        }
    }

    private void deleteOutdatedAssets() {
        File targetRootFolder = getTargetRootFolder();

        // Only remove files supplied by the APK. Game archives, MM assets,
        // saves, configuration, controller settings, and mods belong to the user.
        deleteIfExists(new File(targetRootFolder, "soh.otr"));
        deleteIfExists(new File(targetRootFolder, "soh.o2r"));
        deleteRecursiveIfExists(new File(targetRootFolder, "assets"));
        deleteRecursiveIfExists(new File(targetRootFolder, "nei"));
        deleteRecursiveIfExists(new File(targetRootFolder, "harpoon"));
        deleteIfExists(getSupportFilesMarkerFile(targetRootFolder));
    }

    private File getSupportFilesMarkerFile(File targetRootFolder) {
        return new File(targetRootFolder, SUPPORT_FILES_VERSION_MARKER);
    }

    private boolean isSupportFilesMarkerCurrent() {
        File markerFile = getSupportFilesMarkerFile(getTargetRootFolder());
        if (!markerFile.exists()) {
            return false;
        }

        try (InputStream in = new FileInputStream(markerFile)) {
            byte[] buffer = new byte[(int) markerFile.length()];
            int read = in.read(buffer);
            if (read <= 0) {
                return false;
            }
            String markerVersion = new String(buffer, 0, read).trim();
            return markerVersion.equals(SUPPORT_FILES_VERSION);
        } catch (IOException e) {
            Log.w("setupFiles", "Unable to read support files marker", e);
            return false;
        }
    }

    private void writeSupportFilesMarker(File targetRootFolder) throws IOException {
        File markerFile = getSupportFilesMarkerFile(targetRootFolder);
        try (OutputStream out = new FileOutputStream(markerFile)) {
            out.write(SUPPORT_FILES_VERSION.getBytes());
        }
    }

    private void deleteIfExists(File file) {
        if (file.exists()) {
            if (file.delete()) {
                Log.i("deleteAssets", "Deleted file: " + file.getAbsolutePath());
            } else {
                Log.w("deleteAssets", "Failed to delete file: " + file.getAbsolutePath());
            }
        } else {
            Log.i("deleteAssets", "File not found (skipped): " + file.getAbsolutePath());
        }
    }

    private void deleteRecursiveIfExists(File dir) {
        if (dir.exists()) {
            deleteRecursive(dir);
            Log.i("deleteAssets", "Deleted directory: " + dir.getAbsolutePath());
        } else {
            Log.i("deleteAssets", "Directory not found (skipped): " + dir.getAbsolutePath());
        }
    }

    private void deleteRecursive(File fileOrDirectory) {
        if (fileOrDirectory.isDirectory()) {
            File[] children = fileOrDirectory.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        fileOrDirectory.delete();
    }

    private File getTargetRootFolder() {
        String configuredPath = preferences.getString(PREF_DATA_ROOT_PATH, null);
        if (configuredPath == null || configuredPath.isEmpty()) {
            return getDefaultDataRootFolder();
        }

        return new File(configuredPath);
    }

    private File getDefaultDataRootFolder() {
        return new File(Environment.getExternalStorageDirectory(), DATA_FOLDER_NAME);
    }

    private void updateCurrentDataRootPath() {
        currentDataRootPath = getTargetRootFolder().getAbsolutePath();
        Log.i("setupFiles", "Current data root: " + currentDataRootPath);
    }

    private void beginSetupOrChooseDataRoot() {
        if (!preferences.contains(PREF_DATA_ROOT_PATH)) {
            preferences.edit().putString(PREF_DATA_ROOT_PATH, getDefaultDataRootFolder().getAbsolutePath()).apply();
        }
        beginSetupIfStorageReady();
    }

    private static class DataRootOption {
        final String label;
        final File folder;

        DataRootOption(String label, File folder) {
            this.label = label;
            this.folder = folder;
        }
    }

    private List<DataRootOption> getDataRootOptions() {
        Map<String, DataRootOption> options = new LinkedHashMap<>();
        File defaultFolder = getDefaultDataRootFolder();
        options.put(defaultFolder.getAbsolutePath(),
                new DataRootOption("Internal storage: " + defaultFolder.getAbsolutePath(), defaultFolder));

        File[] externalDirs = getExternalFilesDirs(null);
        if (externalDirs != null) {
            for (File externalDir : externalDirs) {
                if (externalDir == null || !Environment.isExternalStorageRemovable(externalDir)) {
                    continue;
                }

                File volumeRoot = getVolumeRootFromExternalFilesDir(externalDir);
                if (volumeRoot == null) {
                    continue;
                }

                File sdFolder = new File(volumeRoot, DATA_FOLDER_NAME);
                options.put(sdFolder.getAbsolutePath(),
                        new DataRootOption("SD card: " + sdFolder.getAbsolutePath(), sdFolder));
            }
        }

        return new ArrayList<>(options.values());
    }

    private File getVolumeRootFromExternalFilesDir(File externalDir) {
        String path = externalDir.getAbsolutePath();
        String suffix = "/Android/data/" + getPackageName() + "/files";
        int suffixIndex = path.indexOf(suffix);
        if (suffixIndex > 0) {
            return new File(path.substring(0, suffixIndex));
        }

        File parent = externalDir;
        for (int i = 0; i < 4 && parent != null; i++) {
            parent = parent.getParentFile();
        }
        return parent;
    }

    private void showDataRootChooser(boolean restartRequired) {
        List<DataRootOption> options = getDataRootOptions();

        if (options.size() < 2) {
            runOnUiThread(() -> new AlertDialog.Builder(this)
                    .setTitle("Change Data Folder")
                    .setMessage("No alternate writable data folder was found. Insert or mount an SD card and try again.")
                    .setPositiveButton("OK", null)
                    .setCancelable(true)
                    .show());
            return;
        }

        runOnUiThread(() -> {
            LinearLayout layout = new LinearLayout(this);
            layout.setOrientation(LinearLayout.VERTICAL);
            int padding = (int) (20 * getResources().getDisplayMetrics().density);
            layout.setPadding(padding, padding, padding, padding);

            TextView message = new TextView(this);
            message.setText("Choose where Ship of Harkinian stores saves, mods, settings, and support files.");
            message.setTextColor(Color.BLACK);
            layout.addView(message);

            AlertDialog dialog = new AlertDialog.Builder(this)
                    .setTitle("Change Data Folder")
                    .setView(layout)
                    .setNegativeButton("Cancel", null)
                    .setCancelable(true)
                    .create();

            for (DataRootOption option : options) {
                Button optionButton = new Button(this);
                optionButton.setAllCaps(false);
                optionButton.setText(option.label);
                optionButton.setTextColor(Color.BLACK);
                optionButton.setOnClickListener((view) -> {
                    dialog.dismiss();
                    Executors.newSingleThreadExecutor()
                            .execute(() -> applyDataRootSelection(option.folder, restartRequired));
                });
                layout.addView(optionButton);
            }

            dialog.show();
        });
    }

    private void applyDataRootSelection(File targetRootFolder, boolean restartRequired) {
        File previousRoot = getTargetRootFolder();
        Log.i("setupFiles", "Changing data root from " + previousRoot.getAbsolutePath() +
                " to " + targetRootFolder.getAbsolutePath());
        preferences.edit().putString(PREF_DATA_ROOT_PATH, targetRootFolder.getAbsolutePath()).apply();
        updateCurrentDataRootPath();

        if (!ensureTargetRootFolderReady(targetRootFolder)) {
            preferences.edit().putString(PREF_DATA_ROOT_PATH, previousRoot.getAbsolutePath()).apply();
            updateCurrentDataRootPath();
            showSetupFailure("The selected data folder is not writable.");
            return;
        }

        boolean willMigrateData = shouldMigrateExistingRoot(previousRoot, targetRootFolder) ||
                shouldMigrateExistingRoot(getDefaultDataRootFolder(), targetRootFolder);
        if (willMigrateData) {
            showDataRootMigrationDialog();
        }
        migrateExistingRootIfNeeded(previousRoot, targetRootFolder);
        migrateExistingRootIfNeeded(getDefaultDataRootFolder(), targetRootFolder);
        if (willMigrateData) {
            dismissDataRootMigrationDialog();
        }

        if (restartRequired) {
            runOnUiThread(() -> new AlertDialog.Builder(this)
                    .setTitle("Restart Required")
                    .setMessage("Ship of Harkinian will use the new data folder after restarting. Existing data was copied when needed; the old folder was left in place.")
                    .setCancelable(false)
                    .setPositiveButton("Close App", (dialog, which) -> finish())
                    .show());
            return;
        }

        beginSetupIfStorageReady();
    }

    private void showDataRootMigrationDialog() {
        runOnUiThread(() -> {
            if (dataRootMigrationDialog != null && dataRootMigrationDialog.isShowing()) {
                return;
            }

            LinearLayout layout = new LinearLayout(this);
            layout.setOrientation(LinearLayout.VERTICAL);
            int padding = (int) (20 * getResources().getDisplayMetrics().density);
            layout.setPadding(padding, padding, padding, padding);

            ProgressBar progressBar = new ProgressBar(this);
            progressBar.setIndeterminate(true);
            layout.addView(progressBar);

            TextView message = new TextView(this);
            message.setText("Copying saves, mods, settings, and support files. This may take a few minutes.");
            message.setTextColor(Color.BLACK);
            layout.addView(message);

            dataRootMigrationDialog = new AlertDialog.Builder(this)
                    .setTitle("Moving Data Folder")
                    .setView(layout)
                    .setCancelable(false)
                    .create();
            dataRootMigrationDialog.show();
        });
    }

    private void dismissDataRootMigrationDialog() {
        runOnUiThread(() -> {
            if (dataRootMigrationDialog != null && dataRootMigrationDialog.isShowing()) {
                dataRootMigrationDialog.dismiss();
            }
            dataRootMigrationDialog = null;
        });
    }

    private void showSetupProgressDialog(String title, String text) {
        runOnUiThread(() -> {
            if (setupProgressDialog != null && setupProgressDialog.isShowing()) {
                return;
            }

            LinearLayout layout = new LinearLayout(this);
            layout.setOrientation(LinearLayout.VERTICAL);
            int padding = (int) (20 * getResources().getDisplayMetrics().density);
            layout.setPadding(padding, padding, padding, padding);

            ProgressBar progressBar = new ProgressBar(this);
            progressBar.setIndeterminate(true);
            layout.addView(progressBar);

            TextView message = new TextView(this);
            message.setText(text);
            message.setTextColor(Color.BLACK);
            layout.addView(message);

            setupProgressDialog = new AlertDialog.Builder(this)
                    .setTitle(title)
                    .setView(layout)
                    .setCancelable(false)
                    .create();
            setupProgressDialog.show();
        });
    }

    private void dismissSetupProgressDialog() {
        runOnUiThread(() -> {
            if (setupProgressDialog != null && setupProgressDialog.isShowing()) {
                setupProgressDialog.dismiss();
            }
            setupProgressDialog = null;
        });
    }

    private void migrateExistingRootIfNeeded(File sourceRoot, File targetRoot) {
        if (!shouldMigrateExistingRoot(sourceRoot, targetRoot)) {
            return;
        }

        try {
            Log.i("setupFiles", "Copying existing data from " + sourceRoot.getAbsolutePath() +
                    " to " + targetRoot.getAbsolutePath());
            AssetCopyUtil.copyDirectoryContentsNoOverwrite(sourceRoot, targetRoot);
            Log.i("setupFiles", "Copied existing data from: " + sourceRoot.getAbsolutePath());
        } catch (IOException e) {
            Log.e("setupFiles", "Failed to copy existing data from: " + sourceRoot.getAbsolutePath(), e);
            runOnUiThread(() -> Toast.makeText(this, "Could not copy existing data", Toast.LENGTH_LONG).show());
        }
    }

    private boolean shouldMigrateExistingRoot(File sourceRoot, File targetRoot) {
        return sourceRoot != null && targetRoot != null &&
                !sourceRoot.getAbsolutePath().equals(targetRoot.getAbsolutePath()) &&
                sourceRoot.isDirectory() && isDirectoryEmpty(targetRoot);
    }

    private boolean isDirectoryEmpty(File directory) {
        File[] files = directory.listFiles();
        return files == null || files.length == 0;
    }



    // Check if storage permission is granted
    private boolean hasStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11 and above
            return Environment.isExternalStorageManager();
        } else {
            // Android 10 and below
            return ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED &&
                    ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                            == PackageManager.PERMISSION_GRANTED;
        }
    }

    private static final int STORAGE_PERMISSION_REQUEST_CODE = 2296;
    private static final int FILE_PICKER_REQUEST_CODE = 0;

    private void requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ → MANAGE_EXTERNAL_STORAGE
            if (!Environment.isExternalStorageManager()) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, STORAGE_PERMISSION_REQUEST_CODE);
            } else {
                beginSetupOrChooseDataRoot();
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // Android 6–10 → request READ/WRITE at runtime
            ActivityCompat.requestPermissions(this,
                    new String[]{
                            Manifest.permission.READ_EXTERNAL_STORAGE,
                            Manifest.permission.WRITE_EXTERNAL_STORAGE
                    },
                    STORAGE_PERMISSION_REQUEST_CODE);
        } else {
            // Below Android 6 → permissions granted at install time
            beginSetupOrChooseDataRoot();
        }
    }

    private void beginSetupIfStorageReady() {
        updateCurrentDataRootPath();
        File targetRootFolder = getTargetRootFolder();
        Log.i("setupFiles", "Beginning setup for data root: " + targetRootFolder.getAbsolutePath());
        if (!ensureTargetRootFolderReady(targetRootFolder)) {
            showStorageAccessFailure();
            return;
        }

        doVersionCheck();
        checkAndSetupFiles();
    }

    private boolean ensureTargetRootFolderReady(File targetRootFolder) {
        if (!isExternalStorageWritable() || !hasStoragePermission()) {
            return false;
        }

        if (!targetRootFolder.exists() && !targetRootFolder.mkdirs()) {
            Log.e("setupFiles", "Failed to create root folder: " + targetRootFolder.getAbsolutePath());
            return false;
        }

        if (!targetRootFolder.isDirectory() || !targetRootFolder.canWrite()) {
            Log.e("setupFiles", "Root folder is not writable: " + targetRootFolder.getAbsolutePath());
            return false;
        }

        File probeFile = new File(targetRootFolder, ".write_test");
        try (OutputStream out = new FileOutputStream(probeFile)) {
            out.write(0);
        } catch (IOException e) {
            Log.e("setupFiles", "Write probe failed for: " + targetRootFolder.getAbsolutePath(), e);
            return false;
        }

        if (probeFile.exists() && !probeFile.delete()) {
            Log.w("setupFiles", "Failed to delete write probe: " + probeFile.getAbsolutePath());
        }

        return true;
    }

    public void checkAndSetupFiles() {
        File targetRootFolder = getTargetRootFolder();
        File assetsFolder = new File(targetRootFolder, "assets");
        File neiFolder = new File(targetRootFolder, "nei");
        File harpoonFolder = new File(targetRootFolder, "harpoon");
        // Support both .otr (9.0.x) and .o2r (9.2.x) archive formats
        File sohOtrFile = new File(targetRootFolder, "soh.o2r");
        File sohOtrFileLegacy = new File(targetRootFolder, "soh.otr");
        boolean isMissingAssets = !assetsFolder.exists() || assetsFolder.listFiles() == null || assetsFolder.listFiles().length == 0;
        boolean isMissingNei = !neiFolder.exists() || neiFolder.listFiles() == null || neiFolder.listFiles().length == 0;
        boolean isMissingHarpoon = !harpoonFolder.exists() || harpoonFolder.listFiles() == null || harpoonFolder.listFiles().length == 0;
        boolean isMissingSohOtr = !sohOtrFile.exists() && !sohOtrFileLegacy.exists();

        if (!targetRootFolder.exists() || isMissingAssets || isMissingNei || isMissingHarpoon || isMissingSohOtr) {
            showSetupProgressDialog("Preparing Data Folder",
                    "Copying required support files. Please keep Ship of Harkinian open; SD cards may take a few minutes.");
            Executors.newSingleThreadExecutor().execute(() -> setupFilesInBackground(targetRootFolder));
        } else {
            // No setup needed; but always ensure soh.o2r is present from APK assets
            if (!sohOtrFile.exists()) {
                Executors.newSingleThreadExecutor().execute(() -> {
                    try {
                        try (InputStream in = getAssets().open("soh.o2r");
                             OutputStream out = new FileOutputStream(sohOtrFile)) {
                            byte[] buffer = new byte[COPY_BUFFER_SIZE];
                            int read;
                            while ((read = in.read(buffer)) != -1) {
                                out.write(buffer, 0, read);
                            }
                        }
                    } catch (IOException e) {
                        Log.e("setupFiles", "Complete APK is missing required soh.o2r", e);
                        runOnUiThread(() -> showSetupFailure(
                                "The installed APK is incomplete and does not contain soh.o2r. Please install a complete build."));
                        return;
                    }
                    finishSetupWithMmImportPrompt(targetRootFolder);
                });
            } else {
                finishSetupWithMmImportPrompt(targetRootFolder);
            }
        }
    }


    private void setupFilesInBackground(File targetRootFolder) {
        boolean setupFailed = false;
        Log.i("setupFiles", "Copying support files to: " + targetRootFolder.getAbsolutePath());

        if (!ensureTargetRootFolderReady(targetRootFolder)) {
            dismissSetupProgressDialog();
            showStorageAccessFailure();
            return;
        }

        migrateLegacyAppDataIfNeeded(targetRootFolder);

        // Always ensure mods folder exists
        File targetModsDir = new File(targetRootFolder, "mods");
        if (!targetModsDir.exists() && !targetModsDir.mkdirs()) {
            dismissSetupProgressDialog();
            showSetupFailure("Failed to create the mods folder.");
            return;
        }

        // Copy assets/ from internal
        File targetAssetsDir = new File(targetRootFolder, "assets");
        try {
            if (!targetAssetsDir.exists() && !targetAssetsDir.mkdirs()) {
                throw new IOException("Failed to create assets folder: " + targetAssetsDir.getAbsolutePath());
            }
            AssetCopyUtil.copyAssetsToExternal(this, "assets", targetAssetsDir.getAbsolutePath());
            runOnUiThread(() -> Toast.makeText(this, "Assets copied", Toast.LENGTH_SHORT).show());
        } catch (IOException e) {
            e.printStackTrace();
            setupFailed = true;
            runOnUiThread(() -> Toast.makeText(this, "Error copying assets", Toast.LENGTH_LONG).show());
        }

        // NEI's downloadable support package uses root-level nei/ and harpoon/
        // directories. Keep them outside assets/ so the native fork can find them.
        setupFailed |= !copyBundledDirectory("nei", new File(targetRootFolder, "nei"));
        setupFailed |= !copyBundledDirectory("harpoon", new File(targetRootFolder, "harpoon"));

        // Copy soh.o2r from internal assets if bundled (optional)
        try (InputStream assetIn = getAssets().open("soh.o2r")) {
            File targetOtrFile = new File(targetRootFolder, "soh.o2r");
            targetOtrFile.delete();
            try (OutputStream out = new FileOutputStream(targetOtrFile)) {
                byte[] buffer = new byte[COPY_BUFFER_SIZE];
                int read;
                while ((read = assetIn.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                }
            }
            runOnUiThread(() -> Toast.makeText(this, "soh.o2r copied", Toast.LENGTH_SHORT).show());
        } catch (IOException e) {
            Log.e("setupFiles", "Complete APK is missing required soh.o2r", e);
            setupFailed = true;
        }

        if (setupFailed) {
            dismissSetupProgressDialog();
            showSetupFailure("Required support files could not be copied. Please install a complete APK build.");
            return;
        }

        try {
            writeSupportFilesMarker(targetRootFolder);
        } catch (IOException e) {
            Log.e("setupFiles", "Failed to write support files marker", e);
            dismissSetupProgressDialog();
            showSetupFailure("Required support files could not be finalized.");
            return;
        }

        dismissSetupProgressDialog();
        finishSetupWithMmImportPrompt(targetRootFolder);
    }

    private void finishSetupWithMmImportPrompt(File targetRootFolder) {
        runOnUiThread(() -> maybePromptForMmO2rImport(targetRootFolder));
    }

    private void maybePromptForMmO2rImport(File targetRootFolder) {
        File targetMmO2r = new File(targetRootFolder, "mm.o2r");
        String preferenceKey = getMmImportPreferenceKey(targetRootFolder);

        if ((targetMmO2r.isFile() && targetMmO2r.length() > 0) ||
                preferences.getBoolean(preferenceKey, false)) {
            maybePromptForMmFormImport(targetRootFolder);
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle("Import Majora's Mask Assets?")
                .setMessage("SOH Fusion can try to copy mm.o2r from your 2Ship2Harkinian data folder:\n\n" +
                        TWO_SHIP_MM_O2R_PATH + "\n\n" +
                        "This enables Majora's Mask models, animations, audio, and transformation features. " +
                        "SOH Fusion will verify that the copied file exactly matches the source.")
                .setCancelable(false)
                .setPositiveButton("Try Copy", (dialog, which) -> importMmO2rFromTwoShip(targetRootFolder))
                .setNegativeButton("Skip", (dialog, which) -> {
                    preferences.edit().putBoolean(preferenceKey, true).apply();
                    maybePromptForMmFormImport(targetRootFolder);
                })
                .show();
    }

    private String getMmImportPreferenceKey(File targetRootFolder) {
        return PREF_MM_IMPORT_DECISION_COMPLETE + ":" + targetRootFolder.getAbsolutePath();
    }

    private void importMmO2rFromTwoShip(File targetRootFolder) {
        showSetupProgressDialog("Importing Majora's Mask Assets",
                "Copying and verifying mm.o2r from 2Ship2Harkinian. Please keep SOH Fusion open.");

        Executors.newSingleThreadExecutor().execute(() -> {
            String errorMessage = null;
            File source = new File(TWO_SHIP_MM_O2R_PATH);
            File target = new File(targetRootFolder, "mm.o2r");
            File temporaryTarget = new File(targetRootFolder, ".mm.o2r.importing");

            try {
                if (!source.isFile() || source.length() <= 0) {
                    throw new IOException("No mm.o2r was found in the 2S2H folder.");
                }
                if (temporaryTarget.exists() && !temporaryTarget.delete()) {
                    throw new IOException("An incomplete previous import could not be removed.");
                }

                byte[] sourceHash = copyFileAndCalculateSha256(source, temporaryTarget);
                byte[] copiedHash = calculateSha256(temporaryTarget);
                if (source.length() != temporaryTarget.length() || !Arrays.equals(sourceHash, copiedHash)) {
                    throw new IOException("The copied file did not match the source file.");
                }

                if (target.exists() && !target.delete()) {
                    throw new IOException("The existing SOH Fusion mm.o2r could not be replaced.");
                }
                if (!temporaryTarget.renameTo(target)) {
                    throw new IOException("The verified file could not be moved into the SOHFUSION folder.");
                }

                byte[] installedHash = calculateSha256(target);
                if (source.length() != target.length() || !Arrays.equals(sourceHash, installedHash)) {
                    target.delete();
                    throw new IOException("Final verification failed after installing mm.o2r.");
                }
            } catch (IOException e) {
                errorMessage = e.getMessage();
                Log.e("setupFiles", "Unable to import mm.o2r from 2S2H", e);
                temporaryTarget.delete();
            }

            final String finalErrorMessage = errorMessage;
            dismissSetupProgressDialog();
            runOnUiThread(() -> showMmImportResult(targetRootFolder, finalErrorMessage));
        });
    }

    private byte[] copyFileAndCalculateSha256(File source, File target) throws IOException {
        MessageDigest digest = createSha256Digest();
        try (InputStream in = new FileInputStream(source);
             OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[COPY_BUFFER_SIZE];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
                digest.update(buffer, 0, read);
            }
        }
        return digest.digest();
    }

    private byte[] calculateSha256(File file) throws IOException {
        MessageDigest digest = createSha256Digest();
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[COPY_BUFFER_SIZE];
            int read;
            while ((read = in.read(buffer)) != -1) {
                digest.update(buffer, 0, read);
            }
        }
        return digest.digest();
    }

    private MessageDigest createSha256Digest() throws IOException {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new IOException("SHA-256 verification is unavailable on this device.", e);
        }
    }

    private void showMmImportResult(File targetRootFolder, String errorMessage) {
        String preferenceKey = getMmImportPreferenceKey(targetRootFolder);
        if (errorMessage == null) {
            preferences.edit().putBoolean(preferenceKey, true).apply();
            new AlertDialog.Builder(this)
                    .setTitle("MM Assets Imported")
                    .setMessage("mm.o2r was copied from 2Ship2Harkinian and verified successfully. " +
                            "SOH Fusion will now validate its game version as the app starts.")
                    .setCancelable(false)
                    .setPositiveButton("Continue", (dialog, which) ->
                            maybePromptForMmFormImport(targetRootFolder))
                    .show();
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle("MM Assets Were Not Imported")
                .setMessage(errorMessage + "\n\n" +
                        "You can retry, continue without MM assets, or manually place a compatible mm.o2r in:\n" +
                        targetRootFolder.getAbsolutePath())
                .setCancelable(false)
                .setPositiveButton("Retry", (dialog, which) -> importMmO2rFromTwoShip(targetRootFolder))
                .setNegativeButton("Continue Without", (dialog, which) -> {
                    preferences.edit().putBoolean(preferenceKey, true).apply();
                    maybePromptForMmFormImport(targetRootFolder);
                })
                .show();
    }

    private String getMmFormImportPreferenceKey(File targetRootFolder) {
        return PREF_MM_FORM_IMPORT_DECISION_COMPLETE + ":" + targetRootFolder.getAbsolutePath();
    }

    private File getMmFormArchive(File targetRootFolder, String archiveName) {
        return new File(new File(targetRootFolder, "nei"), archiveName);
    }

    private boolean haveMmFormSources() {
        File models = new File(TWO_SHIP_3DS_FORM_MODELS_PATH);
        File textures = new File(TWO_SHIP_3DS_FORM_TEXTURES_PATH);
        File hdTextures = new File(TWO_SHIP_3DS_FORM_HD_TEXTURES_PATH);
        return models.isFile() && models.length() > 0 &&
                textures.isFile() && textures.length() > 0 &&
                hdTextures.isFile() && hdTextures.length() > 0;
    }

    private boolean haveValidMmFormArchives(File targetRootFolder) {
        return isFilteredMmFormArchive(
                getMmFormArchive(targetRootFolder, IMPORTED_MM_FORM_MODELS_NAME), false) &&
                isFilteredMmFormArchive(
                        getMmFormArchive(targetRootFolder, IMPORTED_MM_FORM_TEXTURES_NAME), true);
    }

    private void maybePromptForMmFormImport(File targetRootFolder) {
        File modelTarget = getMmFormArchive(targetRootFolder, IMPORTED_MM_FORM_MODELS_NAME);
        File textureTarget = getMmFormArchive(targetRootFolder, IMPORTED_MM_FORM_TEXTURES_NAME);
        String preferenceKey = getMmFormImportPreferenceKey(targetRootFolder);

        if (haveValidMmFormArchives(targetRootFolder)) {
            maybePromptForMmIconPackImport(targetRootFolder);
            return;
        }

        // The optional 3DS packs may be installed in 2S2H later. If they are not
        // available yet, continue with the original N64 transformation forms.
        if (!haveMmFormSources()) {
            maybePromptForMmIconPackImport(targetRootFolder);
            return;
        }

        // Respect an explicit skip, but repair a partial or invalid prior import.
        if (preferences.getBoolean(preferenceKey, false) &&
                !modelTarget.exists() && !textureTarget.exists()) {
            maybePromptForMmIconPackImport(targetRootFolder);
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle("Import 3DS Transformation Forms?")
                .setMessage("SOH Fusion found the optional 2Ship2Harkinian 3DS Link packs:\n\n" +
                        TWO_SHIP_3DS_FORM_MODELS_PATH + "\n" +
                        TWO_SHIP_3DS_FORM_TEXTURES_PATH + "\n" +
                        TWO_SHIP_3DS_FORM_HD_TEXTURES_PATH + "\n\n" +
                        "Fusion can extract only the Deku, Goron, Zora, and Fierce Deity form resources. " +
                        "The source packs will not be changed or copied in full.")
                .setCancelable(false)
                .setPositiveButton("Import 3DS Forms", (dialog, which) ->
                        importMmFormsFromTwoShip(targetRootFolder))
                .setNegativeButton("Use N64 Forms", (dialog, which) -> {
                    preferences.edit().putBoolean(preferenceKey, true).apply();
                    maybePromptForMmIconPackImport(targetRootFolder);
                })
                .show();
    }

    private void importMmFormsFromTwoShip(File targetRootFolder) {
        showSetupProgressDialog("Importing 3DS Transformation Forms",
                "Extracting and verifying the 2S2H form models and textures. Please keep SOH Fusion open.");

        Executors.newSingleThreadExecutor().execute(() -> {
            String errorMessage = null;
            File modelSource = new File(TWO_SHIP_3DS_FORM_MODELS_PATH);
            File textureSource = new File(TWO_SHIP_3DS_FORM_TEXTURES_PATH);
            File targetDirectory = new File(targetRootFolder, "nei");
            File modelTarget = new File(targetDirectory, IMPORTED_MM_FORM_MODELS_NAME);
            File textureTarget = new File(targetDirectory, IMPORTED_MM_FORM_TEXTURES_NAME);
            File temporaryModels = new File(targetDirectory, ".3ds-form-models.o2r.importing");
            File temporaryTextures = new File(targetDirectory, ".3ds-form-textures.o2r.importing");

            try {
                if (!haveMmFormSources()) {
                    throw new IOException("All three supported 2S2H 3DS Link packs are required.");
                }
                if (!targetDirectory.exists() && !targetDirectory.mkdirs()) {
                    throw new IOException("The Fusion nei folder could not be created.");
                }
                if ((temporaryModels.exists() && !temporaryModels.delete()) ||
                        (temporaryTextures.exists() && !temporaryTextures.delete())) {
                    throw new IOException("An incomplete previous 3DS form import could not be removed.");
                }

                createFilteredMmFormArchive(modelSource, temporaryModels, false);
                createFilteredMmFormArchive(textureSource, temporaryTextures, true);
                if (!isFilteredMmFormArchive(temporaryModels, false) ||
                        !isFilteredMmFormArchive(temporaryTextures, true)) {
                    throw new IOException("The extracted 3DS form archives did not pass validation.");
                }

                byte[] modelHash = calculateSha256(temporaryModels);
                byte[] textureHash = calculateSha256(temporaryTextures);
                if ((modelTarget.exists() && !modelTarget.delete()) ||
                        (textureTarget.exists() && !textureTarget.delete())) {
                    throw new IOException("The existing 3DS form archives could not be replaced.");
                }
                if (!temporaryModels.renameTo(modelTarget) ||
                        !temporaryTextures.renameTo(textureTarget)) {
                    throw new IOException("The verified 3DS form archives could not be installed.");
                }

                if (!Arrays.equals(modelHash, calculateSha256(modelTarget)) ||
                        !Arrays.equals(textureHash, calculateSha256(textureTarget)) ||
                        !haveValidMmFormArchives(targetRootFolder)) {
                    modelTarget.delete();
                    textureTarget.delete();
                    throw new IOException("Final verification failed after installing the 3DS forms.");
                }
            } catch (IOException e) {
                errorMessage = e.getMessage();
                Log.e("setupFiles", "Unable to import 2S2H 3DS transformation forms", e);
                temporaryModels.delete();
                temporaryTextures.delete();
            }

            final String finalErrorMessage = errorMessage;
            dismissSetupProgressDialog();
            runOnUiThread(() -> showMmFormImportResult(targetRootFolder, finalErrorMessage));
        });
    }

    private String getMmFormObjectName(String entryName, boolean textures) {
        String prefix = textures ? "alt/objects/" : "objects/";
        if (!entryName.startsWith(prefix)) {
            return null;
        }
        String remainder = entryName.substring(prefix.length());
        int separator = remainder.indexOf('/');
        if (separator <= 0) {
            return null;
        }
        String objectName = remainder.substring(0, separator);
        return MM_FORM_OBJECT_NAMES.contains(objectName) ? objectName : null;
    }

    private void copyZipEntry(ZipFile sourceArchive, ZipEntry entry, ZipOutputStream out,
                              byte[] buffer) throws IOException {
        ZipEntry filteredEntry = new ZipEntry(entry.getName());
        if (entry.getTime() >= 0) {
            filteredEntry.setTime(entry.getTime());
        }
        out.putNextEntry(filteredEntry);
        try (InputStream in = sourceArchive.getInputStream(entry)) {
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        }
        out.closeEntry();
    }

    private void createFilteredMmFormArchive(File source, File target, boolean textures) throws IOException {
        Set<String> copiedObjects = new HashSet<>();
        try (ZipFile sourceArchive = new ZipFile(source);
             ZipOutputStream out = new ZipOutputStream(new FileOutputStream(target))) {
            Enumeration<? extends ZipEntry> entries = sourceArchive.entries();
            byte[] buffer = new byte[COPY_BUFFER_SIZE];
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String objectName = getMmFormObjectName(entry.getName(), textures);
                if (entry.isDirectory() || objectName == null ||
                        (textures && (MM_FORM_TEXTURE_EXCLUDED_ENTRIES.contains(entry.getName()) ||
                                MM_FORM_TEXTURE_HD_ENTRIES.contains(entry.getName())))) {
                    continue;
                }
                copyZipEntry(sourceArchive, entry, out, buffer);
                copiedObjects.add(objectName);
            }

            if (textures) {
                try (ZipFile hdTextureArchive = new ZipFile(TWO_SHIP_3DS_FORM_HD_TEXTURES_PATH)) {
                    for (String entryName : MM_FORM_TEXTURE_HD_ENTRIES) {
                        ZipEntry entry = hdTextureArchive.getEntry(entryName);
                        if (entry == null || entry.isDirectory()) {
                            throw new IOException("The 3DS HD Link pack is missing a required transformation texture.");
                        }
                        copyZipEntry(hdTextureArchive, entry, out, buffer);
                        copiedObjects.add(getMmFormObjectName(entryName, true));
                    }
                }
            }
        }

        if (!copiedObjects.equals(MM_FORM_OBJECT_NAMES)) {
            target.delete();
            throw new IOException("The 2S2H pack did not contain every supported transformation form.");
        }
    }

    private boolean isFilteredMmFormArchive(File archive, boolean textures) {
        if (!archive.isFile() || archive.length() <= 0) {
            return false;
        }

        Set<String> foundObjects = new HashSet<>();
        Set<String> foundEntryNames = new HashSet<>();
        int foundEntryCount = 0;
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(archive))) {
            ZipEntry entry;
            while ((entry = in.getNextEntry()) != null) {
                if (entry.isDirectory()) {
                    continue;
                }
                String objectName = getMmFormObjectName(entry.getName(), textures);
                if (objectName == null) {
                    return false;
                }
                foundObjects.add(objectName);
                foundEntryNames.add(entry.getName());
                foundEntryCount++;
            }
        } catch (IOException e) {
            Log.w("setupFiles", "Unable to validate imported 3DS form archive", e);
            return false;
        }
        if (textures) {
            if (!foundEntryNames.containsAll(MM_FORM_TEXTURE_HD_ENTRIES)) {
                return false;
            }
            for (String excludedEntry : MM_FORM_TEXTURE_EXCLUDED_ENTRIES) {
                if (foundEntryNames.contains(excludedEntry)) {
                    return false;
                }
            }
        }
        return foundEntryCount > 0 && foundObjects.equals(MM_FORM_OBJECT_NAMES);
    }

    private void showMmFormImportResult(File targetRootFolder, String errorMessage) {
        String preferenceKey = getMmFormImportPreferenceKey(targetRootFolder);
        if (errorMessage == null) {
            preferences.edit().putBoolean(preferenceKey, true).apply();
            new AlertDialog.Builder(this)
                    .setTitle("3DS Transformation Forms Imported")
                    .setMessage("The Deku, Goron, Zora, and Fierce Deity form resources were extracted " +
                            "from the 2S2H packs and verified successfully.")
                    .setCancelable(false)
                    .setPositiveButton("Continue", (dialog, which) ->
                            maybePromptForMmIconPackImport(targetRootFolder))
                    .show();
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle("3DS Transformation Forms Were Not Imported")
                .setMessage(errorMessage + "\n\nYou can retry or continue with the original N64 forms.")
                .setCancelable(false)
                .setPositiveButton("Retry", (dialog, which) -> importMmFormsFromTwoShip(targetRootFolder))
                .setNegativeButton("Use N64 Forms", (dialog, which) -> {
                    preferences.edit().putBoolean(preferenceKey, true).apply();
                    maybePromptForMmIconPackImport(targetRootFolder);
                })
                .show();
    }

    private String getMmIconImportPreferenceKey(File targetRootFolder) {
        return PREF_MM_ICON_IMPORT_DECISION_COMPLETE + ":" + targetRootFolder.getAbsolutePath();
    }

    private File findMmIconPackSource() {
        File mmReloaded = new File(TWO_SHIP_MM_RELOADED_ICON_PACK_PATH);
        if (mmReloaded.isFile() && mmReloaded.length() > 0) {
            return mmReloaded;
        }
        File threeDsHud = new File(TWO_SHIP_3DS_HUD_ICON_PACK_PATH);
        return threeDsHud.isFile() && threeDsHud.length() > 0 ? threeDsHud : null;
    }

    private boolean isMmReloadedIconSource(File source) {
        return source != null && TWO_SHIP_MM_RELOADED_ICON_PACK_PATH.equals(source.getAbsolutePath());
    }

    private void maybePromptForMmIconPackImport(File targetRootFolder) {
        File source = findMmIconPackSource();
        File target = new File(targetRootFolder, IMPORTED_MM_ICON_PACK_NAME);
        File legacyTarget = new File(new File(targetRootFolder, "mods"), LEGACY_IMPORTED_MM_ICON_PACK_NAME);
        String preferenceKey = getMmIconImportPreferenceKey(targetRootFolder);

        // The first test build placed the full HUD pack in mods/, which made it
        // override unrelated assets such as the D-pad. Move it out so the new
        // importer can replace it with an icon-only archive.
        if (!target.exists() && legacyTarget.isFile() && legacyTarget.length() > 0) {
            if (!legacyTarget.renameTo(target)) {
                Log.w("setupFiles", "Unable to move legacy MM icon pack out of mods");
            }
        }

        boolean requireFullHd = isMmReloadedIconSource(source);
        boolean hasFilteredIconArchive = isFilteredMmIconArchive(target, requireFullHd);
        boolean hasAnyFilteredIconArchive = hasFilteredIconArchive || isFilteredMmIconArchive(target, false);
        if (hasFilteredIconArchive ||
                (preferences.getBoolean(preferenceKey, false) &&
                        (!target.exists() || hasAnyFilteredIconArchive))) {
            setupLatch.countDown();
            return;
        }

        // This is optional. If the pack is installed in 2S2H later, ask on a future launch.
        if (source == null) {
            setupLatch.countDown();
            return;
        }

        String sourceName = requireFullHd ? "MM Reloaded v11.0.2 HD" : "3DS HUD Pack";
        String iconResolution = requireFullHd ? "256x256" : "64x64";

        new AlertDialog.Builder(this)
                .setTitle("Import HD Majora's Mask Icons?")
                .setMessage("A 2Ship2Harkinian " + sourceName + " pack was found:\n\n" +
                        source.getAbsolutePath() + "\n\n" +
                        "SOH Fusion can extract only its 24 " + iconResolution + " Majora's Mask inventory icons. " +
                        "The original 32x32 mm.o2r icons remain as a fallback. No other 2S2H mods will be copied.")
                .setCancelable(false)
                .setPositiveButton("Import Icons", (dialog, which) ->
                        importMmIconPackFromTwoShip(targetRootFolder))
                .setNegativeButton("Skip", (dialog, which) -> {
                    preferences.edit().putBoolean(preferenceKey, true).apply();
                    setupLatch.countDown();
                })
                .show();
    }

    private void importMmIconPackFromTwoShip(File targetRootFolder) {
        File selectedSource = findMmIconPackSource();
        String sourceName = isMmReloadedIconSource(selectedSource) ? "MM Reloaded HD" : "3DS HUD";
        showSetupProgressDialog("Importing HD Majora's Mask Icons",
                "Extracting and verifying the 2S2H " + sourceName + " icons. Please keep SOH Fusion open.");

        Executors.newSingleThreadExecutor().execute(() -> {
            String errorMessage = null;
            File source = findMmIconPackSource();
            File target = new File(targetRootFolder, IMPORTED_MM_ICON_PACK_NAME);
            File temporaryTarget = new File(targetRootFolder, ".mm-icons.o2r.importing");

            try {
                if (source == null) {
                    throw new IOException("A supported 2S2H HD icon pack could not be found.");
                }
                if (temporaryTarget.exists() && !temporaryTarget.delete()) {
                    throw new IOException("An incomplete previous icon import could not be removed.");
                }

                createFilteredMmIconArchive(source, temporaryTarget);
                byte[] filteredHash = calculateSha256(temporaryTarget);

                if (target.exists() && !target.delete()) {
                    throw new IOException("The existing imported icon pack could not be replaced.");
                }
                if (!temporaryTarget.renameTo(target)) {
                    throw new IOException("The verified icon pack could not be installed.");
                }

                byte[] installedHash = calculateSha256(target);
                if (!Arrays.equals(filteredHash, installedHash) ||
                        !isFilteredMmIconArchive(target, isMmReloadedIconSource(source))) {
                    target.delete();
                    throw new IOException("Final verification failed after installing the icon pack.");
                }
            } catch (IOException e) {
                errorMessage = e.getMessage();
                Log.e("setupFiles", "Unable to import the 2S2H MM icon pack", e);
                temporaryTarget.delete();
            }

            final String finalErrorMessage = errorMessage;
            dismissSetupProgressDialog();
            runOnUiThread(() -> showMmIconImportResult(targetRootFolder, finalErrorMessage));
        });
    }

    private boolean isMmMaskIconEntry(String entryName) {
        if (!entryName.startsWith(MM_ICON_ARCHIVE_PREFIX)) {
            return false;
        }
        String fileName = entryName.substring(MM_ICON_ARCHIVE_PREFIX.length());
        return !fileName.contains("/") && MM_MASK_ICON_NAMES.contains(fileName);
    }

    private void createFilteredMmIconArchive(File source, File target) throws IOException {
        Set<String> copiedIcons = new HashSet<>();
        try (ZipFile sourceArchive = new ZipFile(source);
             ZipOutputStream out = new ZipOutputStream(new FileOutputStream(target))) {
            byte[] buffer = new byte[COPY_BUFFER_SIZE];
            for (String iconName : MM_MASK_ICON_NAMES) {
                String entryName = MM_ICON_ARCHIVE_PREFIX + iconName;
                ZipEntry entry = sourceArchive.getEntry(entryName);
                if (entry == null || entry.isDirectory()) {
                    continue;
                }

                ZipEntry filteredEntry = new ZipEntry(entryName);
                if (entry.getTime() >= 0) {
                    filteredEntry.setTime(entry.getTime());
                }
                out.putNextEntry(filteredEntry);
                try (InputStream in = sourceArchive.getInputStream(entry)) {
                    int read;
                    while ((read = in.read(buffer)) != -1) {
                        out.write(buffer, 0, read);
                    }
                }
                out.closeEntry();
                copiedIcons.add(entryName);
            }
        }

        if (copiedIcons.size() != MM_MASK_ICON_NAMES.size()) {
            target.delete();
            throw new IOException("The 2S2H pack did not contain all 24 Majora's Mask icons.");
        }
    }

    private boolean isFilteredMmIconArchive(File archive, boolean requireFullHd) {
        if (!archive.isFile() || archive.length() <= 0) {
            return false;
        }

        Set<String> foundIcons = new HashSet<>();
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(archive))) {
            ZipEntry entry;
            while ((entry = in.getNextEntry()) != null) {
                if (entry.isDirectory()) {
                    continue;
                }
                if (!isMmMaskIconEntry(entry.getName())) {
                    return false;
                }
                long uncompressedBytes = 0;
                byte[] buffer = new byte[COPY_BUFFER_SIZE];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    uncompressedBytes += read;
                }
                if (requireFullHd && uncompressedBytes < FULL_HD_MM_ICON_MIN_BYTES) {
                    return false;
                }
                foundIcons.add(entry.getName());
            }
        } catch (IOException e) {
            Log.w("setupFiles", "Unable to validate imported MM icon archive", e);
            return false;
        }
        return foundIcons.size() == MM_MASK_ICON_NAMES.size();
    }

    private void showMmIconImportResult(File targetRootFolder, String errorMessage) {
        String preferenceKey = getMmIconImportPreferenceKey(targetRootFolder);
        if (errorMessage == null) {
            File source = findMmIconPackSource();
            String sourceName = isMmReloadedIconSource(source) ? "MM Reloaded HD" : "the 3DS HUD Pack";
            String iconResolution = isMmReloadedIconSource(source) ? "256x256" : "64x64";
            preferences.edit().putBoolean(preferenceKey, true).apply();
            new AlertDialog.Builder(this)
                    .setTitle("HD MM Icons Imported")
                    .setMessage("The 24 mask icons from 2S2H " + sourceName +
                            " were extracted and verified successfully. Its " + iconResolution +
                            " icons will be used when Alternate Assets is enabled.")
                    .setCancelable(false)
                    .setPositiveButton("Continue", (dialog, which) -> setupLatch.countDown())
                    .show();
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle("HD MM Icons Were Not Imported")
                .setMessage(errorMessage + "\n\nYou can retry or continue with the original mm.o2r icons.")
                .setCancelable(false)
                .setPositiveButton("Retry", (dialog, which) -> importMmIconPackFromTwoShip(targetRootFolder))
                .setNegativeButton("Continue Without", (dialog, which) -> {
                    preferences.edit().putBoolean(preferenceKey, true).apply();
                    setupLatch.countDown();
                })
                .show();
    }

    private boolean copyBundledDirectory(String assetPath, File targetDirectory) {
        try {
            if (!targetDirectory.exists() && !targetDirectory.mkdirs()) {
                throw new IOException("Failed to create support folder: " + targetDirectory.getAbsolutePath());
            }
            AssetCopyUtil.copyAssetsToExternal(this, assetPath, targetDirectory.getAbsolutePath());
            return true;
        } catch (IOException e) {
            Log.e("setupFiles", "Failed to copy bundled " + assetPath + " files", e);
            runOnUiThread(() -> Toast.makeText(this,
                    "Error copying " + assetPath + " support files", Toast.LENGTH_LONG).show());
            return false;
        }
    }

    private void migrateLegacyAppDataIfNeeded(File targetRootFolder) {
        if (preferences.getBoolean(PREF_LEGACY_DATA_MIGRATION_COMPLETE, false)) {
            return;
        }

        File sourceOldRoot = getExternalFilesDir(null);
        File sourceSavesDir = sourceOldRoot == null ? null : new File(sourceOldRoot, "Save");
        if (sourceOldRoot == null || sourceSavesDir == null || !sourceSavesDir.isDirectory()) {
            preferences.edit().putBoolean(PREF_LEGACY_DATA_MIGRATION_COMPLETE, true).apply();
            return;
        }

        Log.i("setupFiles", "Migrating old data without overwriting current files: " + sourceOldRoot.getAbsolutePath());

        File[] sourceFiles = sourceOldRoot.listFiles();
        if (sourceFiles != null) {
            for (File file : sourceFiles) {
                String name = file.getName();
                if (name.equals("assets") || name.equals("soh.otr") || name.equals("oot-mq.otr") ||
                        name.equals("oot.otr") || name.equals("soh.o2r") || name.equals("oot-mq.o2r") ||
                        name.equals("oot.o2r")) {
                    continue;
                }

                File dest = new File(targetRootFolder, name);
                try {
                    if (file.isDirectory()) {
                        AssetCopyUtil.copyDirectoryNoOverwrite(file, dest);
                    } else {
                        AssetCopyUtil.copyFileNoOverwrite(file, dest);
                    }
                    Log.i("setupFiles", "Migrated missing legacy data: " + name);
                } catch (IOException e) {
                    Log.e("setupFiles", "Failed to migrate legacy data: " + name, e);
                }
            }
        }

        preferences.edit().putBoolean(PREF_LEGACY_DATA_MIGRATION_COMPLETE, true).apply();
        runOnUiThread(() -> Toast.makeText(this, "Existing save data checked", Toast.LENGTH_SHORT).show());
    }

    private void showSetupFailure(String message) {
        setupLatch.countDown();
        runOnUiThread(() -> new AlertDialog.Builder(this)
                .setTitle("Setup Failed")
                .setMessage(message)
                .setCancelable(false)
                .setPositiveButton("Close", (dialog, which) -> finish())
                .show());
    }




    private native void nativeHandleSelectedFile(String filePath);
    private native void nativeDialogResult(int result);

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == FILE_PICKER_REQUEST_CODE && resultCode == RESULT_OK && data != null) {
            // Handle file selection
            Uri selectedFileUri = data.getData();
            String fileName = "OOT.z64";

            File destinationDirectory = getTargetRootFolder();
            File destinationFile = new File(destinationDirectory, fileName);

            if (selectedFileUri != null && ensureTargetRootFolderReady(destinationDirectory)) {
                destinationFile.delete();
                try (InputStream in = getContentResolver().openInputStream(selectedFileUri);
                     OutputStream out = new FileOutputStream(destinationFile)) {
                    byte[] buffer = new byte[COPY_BUFFER_SIZE];
                    int bytesRead;
                    while ((bytesRead = in.read(buffer)) != -1) {
                        out.write(buffer, 0, bytesRead);
                    }
                } catch (IOException e) {
                    e.printStackTrace();
                    showSetupFailure("The selected file could not be copied into the SOHFUSION folder.");
                    return;
                }
            } else {
                showStorageAccessFailure();
                return;
            }

            if (destinationFile.exists() && destinationFile.length() > 0) {
                nativeHandleSelectedFile(destinationFile.getPath());
            } else {
                runOnUiThread(() -> Toast.makeText(this, "Failed to copy ROM file", Toast.LENGTH_LONG).show());
                nativeHandleSelectedFile(null);
            }

        } else if (requestCode == STORAGE_PERMISSION_REQUEST_CODE) {
            // Handle MANAGE_EXTERNAL_STORAGE result
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                if (Environment.isExternalStorageManager()) {
                    beginSetupOrChooseDataRoot();
                } else {
                    showStorageAccessFailure();
                }
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        if (requestCode == STORAGE_PERMISSION_REQUEST_CODE) {
            if (hasStoragePermission()) {
                beginSetupOrChooseDataRoot();
            } else {
                showStorageAccessFailure();
            }
        }
    }

    public void showAlertDialog(String title, String message, String btn1, String btn2) {
        runOnUiThread(() -> {
            AlertDialog.Builder builder = new AlertDialog.Builder(this)
                    .setTitle(title)
                    .setMessage(message)
                    .setCancelable(false)
                    .setPositiveButton(btn1, (dialog, which) -> nativeDialogResult(0));
            if (btn2 != null && !btn2.isEmpty()) {
                builder.setNegativeButton(btn2, (dialog, which) -> nativeDialogResult(1));
            }
            AlertDialog dialog = builder.create();
            // FLAG_NOT_FOCUSABLE prevents stealing SDL window focus so onWindowFocusChanged
            // is never called — SDL never fires FOCUS_LOST, ImGui keeps gamepad state intact,
            // SELECT/BACK menu toggle continues to work after dialog dismissal.
            dialog.getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            dialog.show();
        });
    }

    public void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType("*/*");
        runOnUiThread(() -> startActivityForResult(intent, 0));
    }

    public void changeDataFolderFromNative() {
        showDataRootChooser(true);
    }

    public String getAndroidVersionFromNative() {
        String release = Build.VERSION.RELEASE;
        if (release == null || release.isEmpty()) {
            return "Android API " + Build.VERSION.SDK_INT;
        }

        return "Android " + release + " (API " + Build.VERSION.SDK_INT + ")";
    }

    // Check if external storage is available and writable
    private boolean isExternalStorageWritable() {
        String state = Environment.getExternalStorageState();
        return Environment.MEDIA_MOUNTED.equals(state);
    }

    private void showStorageAccessFailure() {
        runOnUiThread(() -> new AlertDialog.Builder(this)
                .setTitle("Storage Permission Required")
                .setMessage("The app needs file access to create and update the SOHFUSION folder. Please grant storage access and try again.")
                .setCancelable(false)
                .setPositiveButton("Open Settings", (dialog, which) -> requestStoragePermission())
                .setNegativeButton("Close", (dialog, which) -> finish())
                .show());
    }

    public native void attachController();
    public native void detachController();
    // Native method for setting button state
    public native void setButton(int button, boolean value);
    public native void setCameraState(int axis, float value);
    private native void setItemButtonPulse();
    private native void setItemButtonHeld(boolean held);

    // Native method for setting joystick axis value
    public native void setAxis(int axis, short value);

    // Signals C++ that the gamepad BACK/SELECT button was pressed, bypassing SDL.
    public native void nativeGamepadBackPressed();
    // Injects a directional menu nav key: dir 0=up 1=down 2=left 3=right.
    public native void nativeMenuNavKey(int dir, boolean pressed);

    public void SetFirstPersonAimingActive(boolean active) {
        mIsAiming = active;
    }

    @Override
    protected void onPause() {
        super.onPause();
        setItemButtonHeld(false);
        mIsAiming = false;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
            int keyCode = event.getKeyCode();
            // SDL loses controller tracking after activity transitions; catch BACK at the activity level.
            boolean isGamepad = (event.getSource() & android.view.InputDevice.SOURCE_GAMEPAD) != 0
                             || (event.getSource() & android.view.InputDevice.SOURCE_JOYSTICK) != 0;
            if (keyCode == KeyEvent.KEYCODE_BUTTON_SELECT ||
                    (keyCode == KeyEvent.KEYCODE_BACK && isGamepad)) {
                nativeGamepadBackPressed();
            }
        }
        return super.dispatchKeyEvent(event);
    }

    private Button button1, button2, button3, button4;
    private Button buttonA, buttonB, buttonX, buttonY;
    private Button buttonDpadUp, buttonDpadDown, buttonDpadLeft, buttonDpadRight;
    private Button buttonLB, buttonRB, buttonZ, buttonStart, buttonBack;
    private Button buttonToggle;
    private FrameLayout leftJoystick;
    private ImageView leftJoystickKnob;
    private View overlayView;
    private ViewGroup buttonGroup;

    // Function to set up the controller overlay (inflate layout and initialize buttons)
    private void setupControllerOverlay() {
        // Inflate the touchcontrol_overlay layout
        LayoutInflater inflater = (LayoutInflater) getSystemService(LAYOUT_INFLATER_SERVICE);
        overlayView = inflater.inflate(R.layout.touchcontrol_overlay, null);

        // Set layout params for overlayView to control positioning and sizing
        FrameLayout.LayoutParams layoutParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
        );
        overlayView.setLayoutParams(layoutParams);
        // Add overlay view to the main layout (you may need to add it to a container like FrameLayout)
        ViewGroup view = (ViewGroup) findViewById(android.R.id.content);
        view.addView(overlayView);
        view.setKeepScreenOn(true);

        buttonGroup = overlayView.findViewById(R.id.button_group);

        buttonA = overlayView.findViewById(R.id.buttonA);
        buttonB = overlayView.findViewById(R.id.buttonB);
        buttonX = overlayView.findViewById(R.id.buttonX);
        buttonY = overlayView.findViewById(R.id.buttonY);

        buttonDpadUp = overlayView.findViewById(R.id.buttonDpadUp);
        buttonDpadDown = overlayView.findViewById(R.id.buttonDpadDown);
        buttonDpadLeft = overlayView.findViewById(R.id.buttonDpadLeft);
        buttonDpadRight = overlayView.findViewById(R.id.buttonDpadRight);

        buttonLB = overlayView.findViewById(R.id.buttonLB);
        buttonRB = overlayView.findViewById(R.id.buttonRB);
        buttonZ = overlayView.findViewById(R.id.buttonZ);

        buttonStart = overlayView.findViewById(R.id.buttonStart);
        buttonBack = overlayView.findViewById(R.id.buttonBack);

        buttonToggle = overlayView.findViewById(R.id.buttonToggle);

        // Initialize joysticks and joystick knobs from the inflated layout
        leftJoystick = overlayView.findViewById(R.id.left_joystick);
        leftJoystickKnob = overlayView.findViewById(R.id.left_joystick_knob);

        FrameLayout rightScreenArea = overlayView.findViewById(R.id.right_screen_area);

        addTouchListener(buttonA, ControllerButtons.BUTTON_A);
        addTouchListener(buttonB, ControllerButtons.BUTTON_B);
        addTouchListener(buttonX, ControllerButtons.BUTTON_X);
        addTouchListener(buttonY, ControllerButtons.BUTTON_Y);

        setupCButtons(buttonDpadUp, ControllerButtons.BUTTON_DPAD_UP);
        setupCButtons(buttonDpadDown, ControllerButtons.BUTTON_DPAD_DOWN);
        setupCButtons(buttonDpadLeft, ControllerButtons.BUTTON_DPAD_LEFT);
        setupCButtons(buttonDpadRight, ControllerButtons.BUTTON_DPAD_RIGHT);

        addTouchListener(buttonLB, ControllerButtons.BUTTON_LB);
        addTouchListener(buttonRB, ControllerButtons.BUTTON_RB);
        addTouchListener(buttonZ, ControllerButtons.AXIS_RT);

        addTouchListener(buttonStart, ControllerButtons.BUTTON_START);
        // BACK uses nativeGamepadBackPressed directly; setButton(BUTTON_BACK) feeds ImGuiKey_GamepadBack, unreliable when SDL Gamepads list is empty.
        buttonBack.setOnTouchListener((v, event) -> {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                nativeGamepadBackPressed();
            }
            return true;
        });


        setupJoystick(leftJoystick, leftJoystickKnob, true);

        setupLookAround(rightScreenArea);

        setupToggleButton(buttonToggle,buttonGroup);
        applyTouchControlsVisibility();

        // Exclude Back/Start from gesture nav zones (they sit at screen edges in landscape).
        // Must be called on each button in its own local coordinates.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ViewTreeObserver.OnGlobalLayoutListener gestureListener = new ViewTreeObserver.OnGlobalLayoutListener() {
                @Override
                public void onGlobalLayout() {
                    overlayView.getViewTreeObserver().removeOnGlobalLayoutListener(this);
                    Rect selfRect = new Rect(0, 0, buttonBack.getWidth(), buttonBack.getHeight());
                    buttonBack.setSystemGestureExclusionRects(Arrays.asList(selfRect));
                    selfRect = new Rect(0, 0, buttonStart.getWidth(), buttonStart.getHeight());
                    buttonStart.setSystemGestureExclusionRects(Arrays.asList(selfRect));
                }
            };
            overlayView.getViewTreeObserver().addOnGlobalLayoutListener(gestureListener);
        }

    }

    public void setTouchControlsDisabledFromNative(boolean disabled) {
        preferences.edit().putBoolean(PREF_TOUCH_CONTROLS_DISABLED, disabled).apply();
        runOnUiThread(this::applyTouchControlsVisibility);
    }

    private void applyTouchControlsVisibility() {
        if (overlayView == null) {
            return;
        }

        boolean touchControlsDisabled = preferences.getBoolean(PREF_TOUCH_CONTROLS_DISABLED, false);
        overlayView.setVisibility(touchControlsDisabled ? View.GONE : View.VISIBLE);

        if (buttonGroup != null) {
            boolean controlsHidden = preferences.getBoolean(PREF_TOUCH_CONTROLS_HIDDEN, false);
            buttonGroup.setVisibility(controlsHidden ? View.INVISIBLE : View.VISIBLE);
            if (controlsHidden || touchControlsDisabled) {
                DisableTouchArea();
                overlayView.setOnTouchListener(null);
            } else {
                EnableTouchArea();
                overlayView.setOnTouchListener((view, e) -> true);
            }
        }

        if (buttonToggle != null) {
            boolean toggleVisible = preferences.getBoolean("toggleButtonVisible", true);
            buttonToggle.setVisibility(!touchControlsDisabled && toggleVisible ? View.VISIBLE : View.GONE);
        }
    }

    private void setupToggleButton(Button button, ViewGroup uiGroup){
        button.setOnClickListener(v -> {
            boolean currentlyHidden = uiGroup.getVisibility() != View.VISIBLE;
            preferences.edit().putBoolean(PREF_TOUCH_CONTROLS_HIDDEN, !currentlyHidden).apply();
            applyTouchControlsVisibility();
        });
    }

    // Function to set a touch listener for each button
    private void addTouchListener(Button button, int buttonNum) {
        // dir>=4: face button nav keys (4=A/select, 5=B/back). -1 = no nav injection.
        int navDir = (buttonNum == ControllerButtons.BUTTON_A) ? 4
                   : (buttonNum == ControllerButtons.BUTTON_B) ? 5 : -1;
        button.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        setButton(buttonNum, true);
                        if (navDir >= 0) nativeMenuNavKey(navDir, true);
                        button.setPressed(true);
                        return true;
                    case MotionEvent.ACTION_UP:
                        setButton(buttonNum, false);
                        if (navDir >= 0) nativeMenuNavKey(navDir, false);
                        button.setPressed(false);
                        return true;
                    case MotionEvent.ACTION_CANCEL:
                        setButton(buttonNum, false);
                        if (navDir >= 0) nativeMenuNavKey(navDir, false);
                        return true;
                }
                return false;
            }
        });
    }

    private void setupCButtons(Button button, int dpadButton) {
        button.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        setButton(dpadButton, true);
                        nativeMenuNavKey(dpadButton - ControllerButtons.BUTTON_DPAD_UP, true);
                        button.setPressed(true);
                        return true;
                    case MotionEvent.ACTION_UP:
                        setButton(dpadButton, false);
                        nativeMenuNavKey(dpadButton - ControllerButtons.BUTTON_DPAD_UP, false);
                        button.setPressed(false);
                        return true;
                    case MotionEvent.ACTION_CANCEL:
                        setButton(dpadButton, false);
                        nativeMenuNavKey(dpadButton - ControllerButtons.BUTTON_DPAD_UP, false);
                        return true;
                }
                return false;
            }
        });
    }

    boolean TouchAreaEnabled = true;

    void DisableTouchArea(){
        TouchAreaEnabled = false;
    }
    void EnableTouchArea(){
        TouchAreaEnabled = true;
    }

    void SetToggleButtonVisible(boolean visible) {
        runOnUiThread(() -> {
            preferences.edit().putBoolean("toggleButtonVisible", visible).apply();
            applyTouchControlsVisibility();
        });
    }

    private void setupLookAround(FrameLayout rightScreenArea) {
        rightScreenArea.setOnTouchListener(new View.OnTouchListener() {
            private float lastX = 0;
            private float lastY = 0;
            private boolean isTouching = false;

            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        // Start tracking the finger's position
                        lastX = event.getX();
                        lastY = event.getY();
                        isTouching = true;
                        if (mIsAiming && TouchAreaEnabled) {
                            setItemButtonPulse();
                            setItemButtonHeld(true);
                        }
                        break;

                    case MotionEvent.ACTION_MOVE:
                        if (isTouching) {
                            // Calculate the change in position (delta)
                            float deltaX = event.getX() - lastX;
                            float deltaY = event.getY() - lastY;

                            // Update the last position
                            lastX = event.getX();
                            lastY = event.getY();

                            // Increase sensitivity by using a larger multiplier
                            // Adjust these multipliers to suit your needs
                            float sensitivityMultiplier = 15; // Higher value for more sensitivity
                            float rx = (deltaX * sensitivityMultiplier);
                            float ry = (deltaY * sensitivityMultiplier);

                            // Send the mapped values to the joystick axes
                            setCameraState(0, rx); // Right stick X axis
                            setCameraState(1, ry); // Right stick Y axis
                        }
                        break;

                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        // Stop tracking the finger's position and reset joystick input
                        isTouching = false;
                        setCameraState(0, 0.0f); // Reset right stick X axis
                        setCameraState(1, 0.0f); // Reset right stick Y axis
                        if (mIsAiming && TouchAreaEnabled) {
                            setItemButtonHeld(false);
                        }
                        break;
                }
                return TouchAreaEnabled; // Event full handled
            }
        });
    }





    // Function to set joystick movement with reset to center when not touched
    private void setupJoystick(FrameLayout joystickLayout, ImageView joystickKnob, boolean isLeft) {
        joystickLayout.post(() -> {
            // Calculate the joystick center once, before any events
            final float joystickCenterX = joystickLayout.getWidth() / 2f;
            final float joystickCenterY = joystickLayout.getHeight() / 2f;

            joystickLayout.setOnTouchListener(new View.OnTouchListener() {
                @Override
                public boolean onTouch(View v, MotionEvent event) {
                    switch (event.getAction()) {
                        case MotionEvent.ACTION_DOWN:
                        case MotionEvent.ACTION_MOVE:
                            // Calculate the joystick movement and move the knob
                            float deltaX = event.getX() - joystickCenterX;
                            float deltaY = event.getY() - joystickCenterY;

                            // Clamp the joystick movement to prevent it from going outside the area
                            float maxRadius = joystickLayout.getWidth() / 2f - joystickKnob.getWidth() / 2f;
                            float distance = (float) Math.sqrt(deltaX * deltaX + deltaY * deltaY);
                            if (distance > maxRadius) {
                                float scale = maxRadius / distance;
                                deltaX *= scale;
                                deltaY *= scale;
                            }

                            joystickKnob.setX(joystickCenterX + deltaX - joystickKnob.getWidth() / 2f);
                            joystickKnob.setY(joystickCenterY + deltaY - joystickKnob.getHeight() / 2f);

                            // Send joystick values to native C code
                            short x = (short) (deltaX / maxRadius * Short.MAX_VALUE);
                            short y = (short) (deltaY / maxRadius * Short.MAX_VALUE);

                            // Send X-axis and Y-axis values
                            setAxis(isLeft ? ControllerButtons.AXIS_LX : ControllerButtons.AXIS_RX, x); // X-axis
                            setAxis(isLeft ? ControllerButtons.AXIS_LY : ControllerButtons.AXIS_RY, y); // Y-axis
                            break;

                        case MotionEvent.ACTION_UP:
                        case MotionEvent.ACTION_CANCEL:
                            // Reset joystick knob to the center position (ensure it's placed correctly)
                            joystickKnob.setX(joystickCenterX - joystickKnob.getWidth() / 2f);
                            joystickKnob.setY(joystickCenterY - joystickKnob.getHeight() / 2f);

                            // Reset joystick values to 0 when released or canceled
                            setAxis(isLeft ? ControllerButtons.AXIS_LX : ControllerButtons.AXIS_RX, (short) 0); // X-axis
                            setAxis(isLeft ? ControllerButtons.AXIS_LY : ControllerButtons.AXIS_RY, (short) 0); // Y-axis
                            break;
                    }
                    return true;
                }
            });
        });


    }

    public void startRumble(int lowIntensity, int highIntensity) {
        int amplitude = Math.max(lowIntensity, highIntensity);
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null) continue;
            int sources = device.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD &&
                (sources & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) continue;
            Vibrator dv = device.getVibrator();
            if (dv != null && dv.hasVibrator()) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    dv.vibrate(VibrationEffect.createOneShot(RUMBLE_MAX_DURATION_MS, amplitude > 0 ? amplitude : VibrationEffect.DEFAULT_AMPLITUDE));
                } else {
                    dv.vibrate(RUMBLE_MAX_DURATION_MS);
                }
                return;
            }
        }
        Vibrator sv = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (sv != null && sv.hasVibrator()) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                sv.vibrate(VibrationEffect.createOneShot(RUMBLE_MAX_DURATION_MS, amplitude > 0 ? amplitude : VibrationEffect.DEFAULT_AMPLITUDE));
            } else {
                sv.vibrate(RUMBLE_MAX_DURATION_MS);
            }
        }
    }

    public void stopRumble() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null) continue;
            int sources = device.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD &&
                (sources & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) continue;
            Vibrator dv = device.getVibrator();
            if (dv != null && dv.hasVibrator()) dv.cancel();
        }
        Vibrator sv = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (sv != null) sv.cancel();
    }

}
