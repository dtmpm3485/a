package com.aruked.kafkalauncher;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import java.util.WeakHashMap;

public final class EspOverlayView extends View {
    private static final int STRIDE = 4;
    private static final int MAX_ENTITIES = 256;
    private static final WeakHashMap<Activity, EspOverlayView> ATTACHED = new WeakHashMap<>();

    private final float[] snapshot = new float[MAX_ENTITIES * STRIDE];
    private final Paint playerPaint = makePaint(Color.rgb(0, 255, 80));
    private final Paint mobPaint = makePaint(Color.rgb(255, 45, 45));
    private final Paint animalPaint = makePaint(Color.rgb(255, 105, 180));

    private EspOverlayView(Activity activity) {
        super(activity);
        setWillNotDraw(false);
        setClickable(false);
        setFocusable(false);
        setFocusableInTouchMode(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        setLayerType(View.LAYER_TYPE_HARDWARE, null);
        float d = getResources().getDisplayMetrics().density;
        playerPaint.setStrokeWidth(Math.max(2.0f, 1.6f * d));
        mobPaint.setStrokeWidth(Math.max(2.0f, 1.6f * d));
        animalPaint.setStrokeWidth(Math.max(2.0f, 1.6f * d));
    }

    public static void attach(Activity activity) {
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            synchronized (ATTACHED) {
                EspOverlayView old = ATTACHED.get(activity);
                if (old != null && old.getParent() != null) return;

                EspOverlayView overlay = new EspOverlayView(activity);
                FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        Gravity.TOP | Gravity.START);
                activity.addContentView(overlay, lp);
                ATTACHED.put(activity, overlay);
            }
        });
    }

    private static Paint makePaint(int color) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setStyle(Paint.Style.STROKE);
        p.setColor(color);
        p.setAlpha(255);
        return p;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        int count;
        try {
            count = nativeFillEspSnapshot(snapshot);
        } catch (Throwable ignored) {
            count = 0;
        }

        final float width = getWidth();
        final float height = getHeight();
        if (width <= 0 || height <= 0) {
            postInvalidateOnAnimation();
            return;
        }

        count = Math.max(0, Math.min(count, MAX_ENTITIES));
        for (int i = 0; i < count; i++) {
            int o = i * STRIDE;
            float cx = snapshot[o] * width;
            float top = snapshot[o + 1] * height;
            float bottom = snapshot[o + 2] * height;
            int category = Math.round(snapshot[o + 3]);

            float boxHeight = Math.abs(bottom - top);
            if (boxHeight < 3.0f || boxHeight > height * 1.5f) continue;

            float halfWidth;
            Paint paint;
            if (category == 1) {
                halfWidth = boxHeight * 0.22f;
                paint = playerPaint;
            } else if (category == 3) {
                halfWidth = boxHeight * 0.30f;
                paint = animalPaint;
            } else if (category == 2) {
                halfWidth = boxHeight * 0.24f;
                paint = mobPaint;
            } else {
                continue;
            }

            float left = cx - halfWidth;
            float right = cx + halfWidth;
            canvas.drawRect(left, top, right, bottom, paint);
        }

        postInvalidateOnAnimation();
    }

    private static native int nativeFillEspSnapshot(float[] output);
}
