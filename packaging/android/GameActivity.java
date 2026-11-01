package org.openrealm.wc3;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import android.content.Intent;
import org.libsdl.app.SDLActivity;

/** SDL owns the native thread and lifecycle; game logic remains in its own library. */
public class GameActivity extends SDLActivity {
    static String[] libraries() {
        return new String[] { "SDL2", "realm_shared", "realm_sheet", "realm_jass", "realm_renderer",
            "realm_game", "realm_menu", "warcraft3" };
    }
    @Override protected String[] getLibraries() {
        if (!getIntent().getBooleanExtra("smoke", false)) return libraries();
        ArrayList<String> libs = new ArrayList<>(Arrays.asList(libraries()));
        libs.add(libs.size() - 1, "realm_smoke");
        return libs.toArray(new String[0]);
    }

    @Override protected String[] getArguments() {
        return arguments(getFilesDir(), getCacheDir(), getIntent());
    }

    /* Normal play waits for fresh loading input; smoke launches use their own fixture data. */
    static String[] arguments(File files, File cache, Intent intent) {
        boolean smoke = intent.getBooleanExtra("smoke", false);
        ArrayList<String> args = new ArrayList<>(Arrays.asList("-data", new File(files, "data").getAbsolutePath(),
            "+set", "com_maxfps", "60", "+set", "vid_native", "1", "+set", "vid_fullscreen", "1",
            "+set", "cl_loading_wait", "1", "+set", "sv_wait_for_begin", "1"));
        if (smoke) {
            args.set(1, new File(cache, "engine-smoke").getAbsolutePath());
            args.addAll(Arrays.asList("+dedicated", "1", "+test", "wc3_smoke.*"));
        }
        if (intent.getBooleanExtra("tft", false)) args.add("-tft");
        if (!smoke) {
            int profile = intent.getIntExtra("profile", 0), particles = intent.getIntExtra("particles", -1);
            if (profile >= 1 && profile <= 2) args.addAll(Arrays.asList("+set", "r_profile", Integer.toString(profile)));
            if (particles == 0 || particles == 1) args.addAll(Arrays.asList("+set", "r_particles", Integer.toString(particles)));
        }
        int frames = intent.getIntExtra("frames", 0);
        String map = intent.getStringExtra("map");
        if (map != null && map.matches("[A-Za-z0-9_./\\\\ ()-]+"))
            args.addAll(Arrays.asList("+map", map));
        if (frames > 0) args.addAll(Arrays.asList("+com_frame_limit", Integer.toString(frames)));
        return args.toArray(new String[0]);
    }
}
