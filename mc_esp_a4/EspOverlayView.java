package org.levimc.launcher.core.minecraft;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.Button;

import java.util.WeakHashMap;

public final class EspOverlayView extends View {
    private static final int STRIDE = 4;
    private static final int MAX = 256;
    private static final WeakHashMap<Activity, EspOverlayView> ATTACHED = new WeakHashMap<>();
    private static volatile boolean nativeLoaded;
    private static volatile boolean espEnabled = true;

    static {
        try {
            System.loadLibrary("mcesp2644");
            nativeLoaded = true;
        } catch (Throwable ignored) {
            nativeLoaded = false;
        }
    }

    private final float[] snapshot = new float[MAX * STRIDE];
    private final Paint player = paint(Color.rgb(0, 255, 80));
    private final Paint mob = paint(Color.rgb(255, 45, 45));
    private final Paint animal = paint(Color.rgb(255, 105, 180));
    private final Paint debugPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private volatile boolean installDone;
    private int retryCount;

    private EspOverlayView(Activity activity) {
        super(activity);
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        setWillNotDraw(false);
        float d = getResources().getDisplayMetrics().density;
        float stroke = Math.max(2.0f, 1.7f * d);
        player.setStrokeWidth(stroke);
        mob.setStrokeWidth(stroke);
        animal.setStrokeWidth(stroke);
        debugPaint.setColor(Color.WHITE);
        debugPaint.setTextSize(Math.max(24f, 12f * d));
        debugPaint.setStyle(Paint.Style.FILL);
        debugPaint.setShadowLayer(4f, 0f, 0f, Color.BLACK);
    }

    private static Paint paint(int color) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setStyle(Paint.Style.STROKE);
        p.setColor(color);
        p.setAlpha(255);
        return p;
    }

    public static void attachAndInstall(Activity activity) {
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            synchronized (ATTACHED) {
                EspOverlayView v = ATTACHED.get(activity);
                if (v == null || v.getParent() == null) {
                    v = new EspOverlayView(activity);
                    FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            Gravity.TOP | Gravity.START);
                    activity.addContentView(v, lp);
                    ATTACHED.put(activity, v);

                    Button toggle = new Button(activity);
                    toggle.setAllCaps(false);
                    toggle.setText("ESP ON");
                    toggle.setTextColor(Color.WHITE);
                    toggle.setTextSize(14f);
                    toggle.setPadding(10, 0, 10, 0);
                    toggle.setBackgroundColor(Color.argb(180, 25, 25, 25));

                    float d = activity.getResources().getDisplayMetrics().density;
                    FrameLayout.LayoutParams tlp = new FrameLayout.LayoutParams(
                            (int)(108 * d),
                            (int)(46 * d),
                            Gravity.TOP | Gravity.START);
                    tlp.leftMargin = (int)(12 * d);
                    tlp.topMargin = (int)(62 * d);

                    toggle.setOnClickListener(btn -> {
                        espEnabled = !espEnabled;
                        try {
                            nativeSetEspEnabled(espEnabled);
                        } catch (Throwable ignored) {}
                        toggle.setText(espEnabled ? "ESP ON" : "ESP OFF");
                        toggle.setBackgroundColor(espEnabled
                                ? Color.argb(190, 20, 90, 35)
                                : Color.argb(190, 110, 30, 30));
                    });
                    activity.addContentView(toggle, tlp);
                }
            }

            if (nativeLoaded) {
                EspOverlayView target;
                synchronized (ATTACHED) {
                    target = ATTACHED.get(activity);
                }
                if (target != null) target.scheduleInstallRetry();
            }
        });
    }

    private void scheduleInstallRetry() {
        if (installDone || retryCount >= 80) return;
        postDelayed(() -> {
            if (installDone) return;
            retryCount++;
            try {
                installDone = nativeInstallEsp();
            } catch (Throwable ignored) {
                installDone = false;
            }
            if (!installDone) {
                scheduleInstallRetry();
            }
            invalidate();
        }, retryCount == 0 ? 800 : 500);
    }

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);
        int count = 0;
        if (nativeLoaded) {
            try {
                count = nativeFillEspSnapshot(snapshot);
            } catch (Throwable ignored) {}
        }
        float w = getWidth(), h = getHeight();

        String dbg = "A8 " + (espEnabled ? "ON" : "OFF") + " native=" + nativeLoaded + " retry=" + retryCount;
        if (nativeLoaded) {
            try {
                String n = nativeGetDebugStatus();
                if (n != null) dbg = n;
            } catch (Throwable ignored) {}
        }
        c.drawText(dbg, 18f, 42f, debugPaint);

        count = Math.max(0, Math.min(count, MAX));
        for (int i = 0; i < count; i++) {
            int o = i * STRIDE;
            float cx = snapshot[o] * w;
            float top = snapshot[o + 1] * h;
            float bottom = snapshot[o + 2] * h;
            int cat = Math.round(snapshot[o + 3]);
            float bh = Math.abs(bottom - top);
            if (bh < 3f || bh > h * 1.5f) continue;

            Paint p;
            float half;
            if (cat == 1) { p = player; half = bh * 0.22f; }
            else if (cat == 2) { p = mob; half = bh * 0.24f; }
            else if (cat == 3) { p = animal; half = bh * 0.30f; }
            else continue;

            c.drawRect(cx - half, top, cx + half, bottom, p);
        }
        postInvalidateOnAnimation();
    }

    private static native boolean nativeInstallEsp();
    private static native int nativeFillEspSnapshot(float[] output);
    private static native String nativeGetDebugStatus();
    private static native void nativeSetEspEnabled(boolean enabled);
    private static native boolean nativeIsEspEnabled();
}
