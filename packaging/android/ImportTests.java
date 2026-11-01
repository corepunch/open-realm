package org.openrealm.wc3;

import android.app.Instrumentation;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.Arrays;

/** On-device transaction tests using the real native MPQ validator and a generated fixture archive. */
public class ImportTests extends Instrumentation {
    private Bundle arguments;
    private File directory;
    private byte[] fixture;

    @Override public void onCreate(Bundle args) { super.onCreate(args); arguments = args; start(); }

    private void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private void rejects(String name, InputStream input, long expected) throws Exception {
        boolean failed = false;
        try { ArchiveStore.copy(directory, name, input, expected, LauncherActivity::checkArchive); }
        catch (IOException error) { failed = true; }
        require(failed, "Expected import rejection: " + name);
        require(Arrays.equals(fixture, Files.readAllBytes(new File(directory, "war3.mpq").toPath())),
            "Rejected import replaced working archive");
        require(directory.list().length == 1, "Rejected import left staging files");
    }

    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            File source = new File(getTargetContext().getFilesDir(), arguments.getString("fixture", "fixture.mpq"));
            fixture = Files.readAllBytes(source.toPath());
            directory = Files.createTempDirectory(getTargetContext().getCacheDir().toPath(), "mpq-tests-").toFile();
            long count = ArchiveStore.copy(directory, "War3.MPQ", new FileInputStream(source), fixture.length,
                LauncherActivity::checkArchive);
            require(count == fixture.length, "Incomplete successful import");
            require(Arrays.equals(fixture, Files.readAllBytes(new File(directory, "war3.mpq").toPath())), "Imported bytes changed");
            ArchiveStore.copy(directory, "war3.mpq", new FileInputStream(source), -1, LauncherActivity::checkArchive);
            rejects("war3.mpq", new ByteArrayInputStream(new byte[64]), 64);
            rejects("war3.mpq", new ByteArrayInputStream(fixture), fixture.length + 1);
            rejects("../war3.mpq", new ByteArrayInputStream(fixture), fixture.length);
            rejects("war3.mpq", new InputStream() {
                @Override public int read() throws IOException { throw new IOException("Interrupted provider stream"); }
            }, -1);
            byte[] alternate = Arrays.copyOf(fixture, fixture.length + 64);
            rejects("war3.mpq", new ByteArrayInputStream(alternate) {
                @Override public void close() throws IOException { throw new IOException("Provider close failed"); }
            }, alternate.length);
            for (String library : GameActivity.libraries()) System.loadLibrary(library);
            Intent launch = new Intent().putExtra("frames", 123);
            String startup = String.join("\n", GameActivity.arguments(getTargetContext().getFilesDir(), getTargetContext().getCacheDir(), launch));
            require(startup.contains("+com_frame_limit\n123"), "Bounded launch lost its frame limit");
            require(!startup.contains("r_profile") && !startup.contains("r_particles"), "Normal launch enables diagnostic overrides");
            Intent diagnostic = new Intent().putExtra("profile", 2).putExtra("particles", 0);
            startup = String.join("\n", GameActivity.arguments(getTargetContext().getFilesDir(), getTargetContext().getCacheDir(), diagnostic));
            require(startup.contains("r_profile\n2") && startup.contains("r_particles\n0"), "Diagnostic overrides were lost");
            diagnostic.putExtra("profile", 3).putExtra("particles", 9);
            startup = String.join("\n", GameActivity.arguments(getTargetContext().getFilesDir(), getTargetContext().getCacheDir(), diagnostic));
            require(!startup.contains("r_profile") && !startup.contains("r_particles"), "Invalid diagnostic overrides were accepted");
            diagnostic.putExtra("smoke", true).putExtra("profile", 2).putExtra("particles", 0);
            startup = String.join("\n", GameActivity.arguments(getTargetContext().getFilesDir(), getTargetContext().getCacheDir(), diagnostic));
            require(!startup.contains("r_profile") && !startup.contains("r_particles"), "Smoke launch inherited diagnostic overrides");
            startup = String.join("\n", GameActivity.arguments(getTargetContext().getFilesDir(), getTargetContext().getCacheDir(), launch));
            require(!startup.contains("+map") && !startup.contains("-tft"), "Normal launch must open the edition's menu");
            require(!startup.contains("r_render_scale") && !startup.contains("r_stats") && !startup.contains("r_vsync"),
                "Normal launch must retain the user's rendering preferences");
            require(startup.contains("com_maxfps\n60") && startup.contains("cl_loading_wait\n1") &&
                startup.contains("sv_wait_for_begin\n1"), "Normal launch must retain its frame cap and loading acknowledgement");
            launch.putExtra("tft", true);
            startup = String.join("\n", GameActivity.arguments(getTargetContext().getFilesDir(), getTargetContext().getCacheDir(), launch));
            require(startup.contains("-tft") && !startup.contains("+map"), "TFT launch must open its menu");
            launch.putExtra("map", "Maps/Campaign/Prologue01.w3m");
            startup = String.join("\n", GameActivity.arguments(getTargetContext().getFilesDir(), getTargetContext().getCacheDir(), launch));
            require(startup.contains("+map\nMaps/Campaign/Prologue01.w3m"), "Explicit diagnostic map was lost");
            startup = String.join("\n", GameActivity.arguments(getTargetContext().getFilesDir(), getTargetContext().getCacheDir(), new Intent().putExtra("smoke", true)));
            require(startup.contains("+test\nwc3_smoke.*") && !startup.contains("+map") && !startup.contains("-tft"),
                "Smoke launch must use the fixture without starting a retail map");
            LauncherActivity.installAssets(getTargetContext(), "share");
            File smoke = new File(getTargetContext().getCacheDir(), "engine-smoke");
            ArchiveStore.copy(smoke, "war3.mpq", new FileInputStream(source), fixture.length, LauncherActivity::checkArchive);
            File log = new File(getTargetContext().getFilesDir(), "engine.log");
            Files.deleteIfExists(log.toPath());
            Intent game = new Intent(getTargetContext(), GameActivity.class);
            game.putExtra("smoke", true);
            game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            long started = System.currentTimeMillis();
            getTargetContext().startActivity(game);
            long deadline = android.os.SystemClock.elapsedRealtime() + 30000;
            boolean completed = false;
            while (android.os.SystemClock.elapsedRealtime() < deadline) {
                String text = log.isFile() ? new String(Files.readAllBytes(log.toPath()), "UTF-8") : "";
                if (text.contains("6/6 assertions passed in 4 test(s)")) { completed = true; break; }
                Thread.sleep(100);
            }
            require(completed, "Native engine smoke tests did not complete; inspect files/engine.log");
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                ActivityManager manager = (ActivityManager)getTargetContext().getSystemService(android.content.Context.ACTIVITY_SERVICE);
                boolean exited = false;
                while (android.os.SystemClock.elapsedRealtime() < deadline && !exited) {
                    for (ApplicationExitInfo info : manager.getHistoricalProcessExitReasons(getTargetContext().getPackageName(), 0, 8)) {
                        if (info.getProcessName().endsWith(":game") && info.getTimestamp() >= started) {
                            require(info.getReason() == ApplicationExitInfo.REASON_EXIT_SELF && info.getStatus() == 0,
                                "Native process did not exit cleanly: reason=" + info.getReason() + " status=" + info.getStatus());
                            exited = true;
                            break;
                        }
                    }
                    if (!exited) Thread.sleep(100);
                }
                require(exited, "Native process exit was not recorded");
            }
            result.putString("result", "PASS");
            result.putString("stream", "MPQ imports: case normalization, known/unknown size, invalid archive, truncation, "
                + "unsafe filename, interrupted stream, preservation/cleanup PASS; all runtime modules loaded; "
                + "APK startup arguments, diagnostic map and smoke isolation PASS; "
                + "real SDL_main / server / GetGameAPI startup: 4 smoke tests and clean process exit PASS\n");
            finish(-1, result);
        } catch (Throwable error) {
            result.putString("result", "FAIL");
            result.putString("stream", Log.getStackTraceString(error));
            finish(1, result);
        } finally {
            if (directory != null) {
                File[] files = directory.listFiles();
                if (files != null) for (File file : files) file.delete();
                directory.delete();
            }
        }
    }
}
