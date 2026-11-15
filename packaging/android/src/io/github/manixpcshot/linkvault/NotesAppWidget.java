package io.github.manixpcshot.linkvault;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.SystemClock;
import android.view.View;
import android.widget.RemoteViews;

public final class NotesAppWidget extends AppWidgetProvider {
    private static final String PREFS = "linkvault_note_widget";
    private static final String EXPIRE = "io.github.manixpcshot.linkvault.NOTES_WIDGET_EXPIRE";
    private static final int FLAGS = PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE;

    public static void publish(Context context, String title, String preview, long deadline) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("title", title)
                .putString("preview", preview)
                .putLong("deadline", deadline)
                .apply();
        refresh(context);
    }

    @Override
    public void onUpdate(Context context, AppWidgetManager manager, int[] ids) {
        refresh(context);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        super.onReceive(context, intent);
        String action = intent.getAction();
        if (EXPIRE.equals(action) || Intent.ACTION_BOOT_COMPLETED.equals(action)
                || Intent.ACTION_TIME_CHANGED.equals(action) || Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) {
            refresh(context);
        }
    }

    @Override
    public void onDisabled(Context context) {
        cancelExpiry(context);
    }

    private static PendingIntent expiryIntent(Context context) {
        Intent intent = new Intent(context, NotesAppWidget.class).setAction(EXPIRE);
        return PendingIntent.getBroadcast(context, 31, intent, FLAGS);
    }

    private static void cancelExpiry(Context context) {
        AlarmManager alarms = context.getSystemService(AlarmManager.class);
        if (alarms != null) alarms.cancel(expiryIntent(context));
    }

    private static void refresh(Context context) {
        AppWidgetManager manager = AppWidgetManager.getInstance(context);
        if (manager == null) return;
        int[] ids = manager.getAppWidgetIds(new ComponentName(context, NotesAppWidget.class));
        if (ids.length == 0) return;

        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String title = prefs.getString("title", "");
        String preview = prefs.getString("preview", "");
        long deadline = prefs.getLong("deadline", 0L);
        long endMillis = deadline > 0 ? deadline * 1000L : 0L;
        long remaining = endMillis - System.currentTimeMillis();

        RemoteViews views = new RemoteViews(context.getPackageName(), R.layout.linkvault_notes_widget);
        views.setTextViewText(R.id.note_title, title.isEmpty() ?
                context.getString(R.string.linkvault_widget_empty) : title);
        views.setTextViewText(R.id.note_preview, preview);
        views.setViewVisibility(R.id.note_preview, preview.isEmpty() ? View.GONE : View.VISIBLE);
        views.setViewVisibility(R.id.note_timer, remaining > 0 ? View.VISIBLE : View.GONE);
        views.setViewVisibility(R.id.note_finished, deadline > 0 && remaining <= 0 ? View.VISIBLE : View.GONE);
        views.setChronometerCountDown(R.id.note_timer, true);
        views.setChronometer(R.id.note_timer,
                SystemClock.elapsedRealtime() + Math.max(0L, remaining), null, remaining > 0);

        Intent open = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
        if (open != null) views.setOnClickPendingIntent(R.id.note_root, PendingIntent.getActivity(context, 30, open, FLAGS));
        manager.updateAppWidget(ids, views);

        AlarmManager alarms = context.getSystemService(AlarmManager.class);
        if (alarms == null) return;
        PendingIntent expiry = expiryIntent(context);
        alarms.cancel(expiry);
        if (remaining > 0) alarms.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, endMillis, expiry);
    }
}
