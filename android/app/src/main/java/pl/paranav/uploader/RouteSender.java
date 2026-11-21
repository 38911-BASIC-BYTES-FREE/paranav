package pl.paranav.uploader;

import android.app.Activity;
import android.app.AlertDialog;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.nio.charset.StandardCharsets;
import java.util.List;

/** "Send to ParaNav" for a planned route (dialog with options + progress). */
final class RouteSender {
    private RouteSender() {}

    static void send(Activity a, String name, List<CupFile.Wpt> pts) {
        if (pts.size() < 2) {
            new AlertDialog.Builder(a).setMessage(Tr.t("Trasa musi mieć co najmniej 2 punkty.", "A route needs at least 2 points."))
                    .setPositiveButton("OK", null).show();
            return;
        }
        if (!Conn.connected()) {
            new AlertDialog.Builder(a)
                    .setTitle(Tr.t("Nie połączono z ParaNav", "Not connected to ParaNav"))
                    .setMessage(Tr.t("Trasa jest zapisana w telefonie. Żeby ją wysłać, wróć do ekranu głównego, "
                                    + "naciśnij „Połącz z ParaNav” i wyślij ją z listy „Moje trasy”.",
                            "The route is saved on the phone. To send it, go back to the main screen, press "
                                    + "\"Connect to ParaNav\" and send it from \"My routes\"."))
                    .setPositiveButton("OK", null).show();
            return;
        }
        // airspace check against the zones loaded in the app (map overlay), then the send dialog
        new Thread(() -> {
            List<RouteCheck.Hit> hits = RouteCheck.check(Store.loadAirspace(a), pts);
            String warn = RouteCheck.summary(hits);
            a.runOnUiThread(() -> showDialog(a, name, pts, warn));
        }).start();
    }

    private static void showDialog(Activity a, String name, List<CupFile.Wpt> pts, String warn) {
        int pad = (int) (16 * a.getResources().getDisplayMetrics().density);
        LinearLayout l = new LinearLayout(a);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(pad, pad / 2, pad, 0);
        CheckBox act = new CheckBox(a);
        act.setText(Tr.t("Ustaw jako aktywną trasę w ParaNav", "Make it the active route on ParaNav"));
        act.setChecked(true);
        CheckBox repl = new CheckBox(a);
        repl.setText(Tr.t("Usuń inne trasy z ParaNav", "Delete other routes on ParaNav"));
        if (Conn.canActivateRoute()) {
            l.addView(act);
        } else {
            act.setChecked(false);
            TextView t = new TextView(a);
            t.setText(Tr.t("Firmware ParaNav starszy niż 0.5: wybierz trasę w urządzeniu (Menu > Nawigacja).",
                    "ParaNav firmware older than 0.5: choose the route on the device (Menu > Navigation)."));
            l.addView(t);
        }
        l.addView(repl);
        if (!warn.isEmpty()) {
            TextView w = new TextView(a);
            w.setText("⚠ " + warn);
            w.setTextColor(0xFFB00020);
            w.setPadding(0, pad / 2, 0, 0);
            l.addView(w);
        }
        new AlertDialog.Builder(a)
                .setTitle(Tr.t("Wyślij „", "Send \"") + name + Tr.t("”", "\""))
                .setView(l)
                .setPositiveButton(Tr.t("Wyślij", "Send"), (d, w) -> doSend(a, name, pts, act.isChecked(), repl.isChecked()))
                .setNegativeButton(Tr.t("Anuluj", "Cancel"), null)
                .show();
    }

    /** Firmware >= 0.5.4 appends " AIRSPACE <n>" to "OK END ..." after checking an activated route. */
    static int deviceConflicts(String reply) {
        if (reply == null) return 0;
        int i = reply.lastIndexOf(" AIRSPACE ");
        if (i < 0) return 0;
        try {
            return Integer.parseInt(reply.substring(i + 10).trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private static void doSend(Activity a, String name, List<CupFile.Wpt> pts, boolean activate, boolean replace) {
        byte[] data = CupFile.write(pts).getBytes(StandardCharsets.UTF_8);
        ProgressBar bar = new ProgressBar(a, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        int pad = (int) (16 * a.getResources().getDisplayMetrics().density);
        bar.setPadding(pad, pad, pad, pad);
        AlertDialog dlg = new AlertDialog.Builder(a).setTitle(Tr.t("Wysyłam…", "Sending…")).setView(bar).setCancelable(false).show();
        int flags = (activate && Conn.canActivateRoute() ? Uploader.FLAG_ACTIVATE : 0) | (replace ? Uploader.FLAG_REPLACE : 0);
        Conn.worker.execute(() -> {
            String msg;
            try {
                String r = Conn.uploader.upload(Proto.KIND_ROUTE, name + ".cup", data, flags,
                        (done, total) -> a.runOnUiThread(() -> bar.setProgress(total > 0 ? (int) (done * 1000 / total) : 0)));
                msg = Tr.t("Trasa wysłana do ParaNav", "Route sent to ParaNav")
                        + (activate && Conn.canActivateRoute() ? Tr.t(" i ustawiona jako aktywna.", " and made active.") : ".");
                int n = deviceConflicts(r);
                if (n > 0)
                    msg += "\n\n⚠ " + String.format(java.util.Locale.ROOT, Tr.t("ParaNav: trasa przecina %d stref(y) aktywnych dziś "
                            + "lub bez planu – szczegóły na ekranie urządzenia.", "ParaNav: the route crosses %d zone(s) active today "
                            + "or without a schedule – details on the device screen."), n);
            } catch (Exception e) {
                msg = Tr.t("Błąd: ", "Error: ") + e.getMessage();
            }
            String m = msg;
            a.runOnUiThread(() -> {
                dlg.dismiss();
                new AlertDialog.Builder(a).setMessage(m).setPositiveButton("OK", null).show();
            });
        });
    }
}
