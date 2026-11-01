package org.openrealm.probe;

import android.os.Bundle;
import android.graphics.Color;
import android.view.ViewGroup;
import android.widget.TextView;
import org.libsdl.app.SDLActivity;

/** Diagnostic overlay only; the SDL surface below renders the production shader. */
public class ProbeActivity extends SDLActivity {
    private TextView report;

    @Override
    protected String[] getLibraries() { return new String[] { "SDL2", "main" }; }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        report = new TextView(this);
        report.setText("OpenRealm: creating SDL2 / GLES3 context...");
        report.setTextSize(16);
        report.setTextColor(Color.WHITE);
        report.setBackgroundColor(0xd916202c);
        report.setPadding(20, 12, 20, 12);
        report.setFitsSystemWindows(true);
        addContentView(report, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    public void showReport(final String text) {
        runOnUiThread(new Runnable() {
            @Override public void run() { report.setText(text); }
        });
    }
}
