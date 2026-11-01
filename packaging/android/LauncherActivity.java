package org.openrealm.wc3;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.util.ArrayList;

/** Platform launcher: imports local archives before creating the SDL/game process. */
public class LauncherActivity extends Activity {
    static { System.loadLibrary("mpqcheck"); }
    static native boolean checkArchive(String path);
    private TextView status;
    private Button choose, roc, tft;
    private boolean busy;

    private File dataDirectory() { return new File(getFilesDir(), "data"); }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(32, 32, 32, 32);
        TextView title = new TextView(this);
        title.setText("OpenRealm — Warcraft III");
        title.setTextSize(26);
        panel.addView(title);
        status = new TextView(this);
        status.setTextSize(18);
        panel.addView(status);
        choose = new Button(this);
        choose.setText("Import Warcraft III MPQs");
        choose.setOnClickListener(view -> {
            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            picker.setType("*/*");
            picker.addCategory(Intent.CATEGORY_OPENABLE);
            picker.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            startActivityForResult(picker, 1);
        });
        panel.addView(choose);
        roc = new Button(this);
        roc.setText("Play Reign of Chaos");
        roc.setOnClickListener(view -> startGame(false));
        panel.addView(roc);
        tft = new Button(this);
        tft.setText("Play The Frozen Throne");
        tft.setOnClickListener(view -> startGame(true));
        panel.addView(tft);
        setContentView(panel);
        updateStatus();
        if (getIntent().getBooleanExtra("play", false) && roc.isEnabled())
            startGame(getIntent().getBooleanExtra("tft", false));
    }

    private void updateStatus() {
        File data = dataDirectory();
        boolean base = new File(data, "war3.mpq").isFile();
        status.setText(base ? "War3.mpq is installed. Import additional language or expansion MPQs as needed."
            : "Import your classic Warcraft III archives. War3.mpq is required. Game data is not included.");
        choose.setEnabled(!busy);
        roc.setEnabled(base && !busy);
        tft.setEnabled(base && new File(data, "war3x.mpq").isFile() && !busy);
    }

    private void failed(Exception error) {
        Log.e("OpenRealm", "Import/startup failed", error);
        busy = false;
        updateStatus();
        new AlertDialog.Builder(this).setTitle("Could not prepare Warcraft III").setMessage(error.getMessage())
            .setPositiveButton("OK", null).show();
    }

    @Override protected void onActivityResult(int request, int result, Intent intent) {
        super.onActivityResult(request, result, intent);
        if (request != 1 || result != RESULT_OK || intent == null) return;
        ArrayList<Uri> uris = new ArrayList<>();
        if (intent.getClipData() != null) {
            for (int i = 0; i < intent.getClipData().getItemCount(); i++) uris.add(intent.getClipData().getItemAt(i).getUri());
        } else if (intent.getData() != null) uris.add(intent.getData());
        busy = true;
        updateStatus();
        new Thread(() -> {
            try {
                for (Uri uri : uris) {
                    String name;
                    long size = -1;
                    try (Cursor cursor = getContentResolver().query(uri,
                            new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE }, null, null, null)) {
                        if (cursor == null || !cursor.moveToFirst()) throw new IOException("Cannot read selected file metadata");
                        name = ArchiveStore.archiveName(cursor.getString(0));
                        if (!cursor.isNull(1)) size = cursor.getLong(1);
                    }
                    final String current = name;
                    runOnUiThread(() -> status.setText("Importing " + current + "…"));
                    ArchiveStore.copy(dataDirectory(), name, getContentResolver().openInputStream(uri), size,
                        LauncherActivity::checkArchive);
                    Log.i("OpenRealm", "Imported " + name);
                }
                runOnUiThread(() -> { busy = false; updateStatus(); });
            } catch (Exception error) { runOnUiThread(() -> failed(error)); }
        }, "MPQ import").start();
    }

    /* Assets stay separate from retail data and are refreshed from this APK before each launch. */
    static void installAssets(Context context, String path) throws IOException {
        String[] children = context.getAssets().list(path);
        File destination = new File(context.getFilesDir(), path);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) throw new IOException("Cannot create " + path);
            for (String child : children) installAssets(context, path + "/" + child);
        } else {
            try (InputStream input = context.getAssets().open(path); FileOutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[16384];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            }
        }
    }

    private void startGame(boolean expansion) {
        /* A bounded diagnostic run must not affect subsequent Play button launches. */
        final int frames = getIntent().getIntExtra("frames", 0);
        final int profile = getIntent().getIntExtra("profile", 0), particles = getIntent().getIntExtra("particles", -1);
        final String map = getIntent().getStringExtra("map");
        getIntent().removeExtra("frames");
        getIntent().removeExtra("map");
        getIntent().removeExtra("profile");
        getIntent().removeExtra("particles");
        busy = true;
        updateStatus();
        status.setText("Preparing game…");
        new Thread(() -> {
            try {
                installAssets(this, "share");
                runOnUiThread(() -> {
                    Intent game = new Intent(this, GameActivity.class);
                    game.putExtra("tft", expansion);
                    game.putExtra("frames", frames);
                    game.putExtra("profile", profile);
                    game.putExtra("particles", particles);
                    if (map != null) game.putExtra("map", map);
                    startActivity(game);
                    busy = false;
                    updateStatus();
                });
            } catch (Exception error) { runOnUiThread(() -> failed(error)); }
        }, "Game assets").start();
    }
}
