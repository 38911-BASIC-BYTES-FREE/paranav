package pl.paranav.uploader;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Locale;

/** Route planning on the map: tap = add point, tap a point = edit, hold + drag = move. */
public class PlannerActivity extends Activity implements MapView.Listener {
    static final String EXTRA_ROUTE = "route";  // route name (file in Store.ROUTES); none = new route
    private static final int REQ_EXPORT = 1, REQ_AIRSPACE = 2, REQ_WAYPOINTS = 3;

    private MapView map;
    private TextView title, summary;
    private LinearLayout list;
    private final List<CupFile.Wpt> pts = new ArrayList<>();
    private final Deque<List<CupFile.Wpt>> undo = new ArrayDeque<>();
    private String name;
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("planner", MODE_PRIVATE);
        String n = getIntent().getStringExtra(EXTRA_ROUTE);
        if (n != null) {
            name = n;
            try {
                pts.addAll(Store.loadRoute(Store.routeFile(this, n)));
            } catch (IOException e) {
                toast(Tr.t("Nie mogę wczytać trasy: ", "Cannot load the route: ") + e.getMessage());
            }
        } else {
            name = Store.uniqueRouteName(this, Tr.t("Trasa", "Route"));
        }

        int pad = dp(8);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(pad, 0, 0, 0);
        title = new TextView(this);
        title.setTextSize(18);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setOnClickListener(v -> rename());
        bar.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        bar.addView(smallButton("⋮", v -> menu()));
        root.addView(bar);

        FrameLayout mapBox = new FrameLayout(this);
        map = new MapView(this);
        map.setListener(this);
        map.setRoute(pts);
        mapBox.addView(map);
        LinearLayout zoom = new LinearLayout(this);
        zoom.setOrientation(LinearLayout.VERTICAL);
        zoom.addView(smallButton("+", v -> map.zoomBy(1)));
        zoom.addView(smallButton("−", v -> map.zoomBy(-1)));
        zoom.addView(smallButton("⤢", v -> map.fitRoute()));
        zoom.addView(smallButton("↶", v -> undo()));
        FrameLayout.LayoutParams zp = new FrameLayout.LayoutParams(-2, -2, Gravity.END | Gravity.CENTER_VERTICAL);
        zp.setMargins(0, 0, pad, 0);
        mapBox.addView(zoom, zp);
        root.addView(mapBox, new LinearLayout.LayoutParams(-1, 0, 3));

        summary = new TextView(this);
        summary.setTextSize(15);
        summary.setTypeface(Typeface.DEFAULT_BOLD);
        summary.setPadding(pad, pad / 2, pad, 0);
        root.addView(summary);
        ScrollView sv = new ScrollView(this);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(pad, 0, pad, 0);
        sv.addView(list);
        root.addView(sv, new LinearLayout.LayoutParams(-1, 0, 2));

        LinearLayout buttons = new LinearLayout(this);
        Button save = new Button(this);
        save.setAllCaps(false);
        save.setText(Tr.t("Zapisz", "Save"));
        save.setOnClickListener(v -> {
            save();
            toast(Tr.t("Zapisano w telefonie: ", "Saved on the phone: ") + name);
        });
        Button send = new Button(this);
        send.setAllCaps(false);
        send.setText(Tr.t("Wyślij do ParaNav", "Send to ParaNav"));
        send.setOnClickListener(v -> {
            save();
            RouteSender.send(this, name, pts);
        });
        buttons.addView(save, new LinearLayout.LayoutParams(0, -2, 1));
        buttons.addView(send, new LinearLayout.LayoutParams(0, -2, 1));
        root.addView(buttons);
        setContentView(root);

        map.setShowAirspace(prefs.getBoolean("showAirspace", true));
        map.setShowWaypoints(prefs.getBoolean("showWaypoints", true));
        if (pts.isEmpty()) {
            map.setView(prefs.getFloat("lat", 52.0f), prefs.getFloat("lon", 19.4f), prefs.getFloat("zoom", 6f));
            toast(Tr.t("Dotknij mapy, żeby dodać punkt trasy", "Tap the map to add a route point"));
        } else {
            map.post(map::fitRoute);
        }
        loadOverlays();
        refresh();
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private Button smallButton(String s, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(s);
        b.setTextSize(20);
        b.setAllCaps(false);
        b.setMinWidth(dp(52));
        b.setMinimumWidth(dp(52));
        b.setOnClickListener(l);
        return b;
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }

    private void loadOverlays() {
        new Thread(() -> {
            List<OpenAir.Zone> z = Store.loadAirspace(this);
            List<CupFile.Wpt> w = Store.loadWaypoints(this);
            runOnUiThread(() -> map.setOverlays(z, w));
        }).start();
    }

    // ------------------------------------------------------------------ editing
    private void snapshot() {
        List<CupFile.Wpt> c = new ArrayList<>();
        for (CupFile.Wpt w : pts) c.add(w.copy());
        undo.push(c);
        while (undo.size() > 50) undo.removeLast();
    }

    private void undo() {
        if (undo.isEmpty()) return;
        pts.clear();
        pts.addAll(undo.pop());
        changed();
    }

    private void changed() {
        save();
        map.invalidate();
        refresh();
    }

    private void save() {
        try {
            Store.write(Store.routeFile(this, name), CupFile.write(pts).getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            toast(Tr.t("Nie mogę zapisać: ", "Cannot save: ") + e.getMessage());
        }
    }

    @Override
    public void onMapTap(double lat, double lon, int routeIdx, CupFile.Wpt wpt) {
        if (routeIdx >= 0) {
            editPoint(routeIdx);
            return;
        }
        snapshot();
        if (wpt != null) {  // tapped a known waypoint: use it (name, elevation, landing)
            pts.add(wpt.copy());
        } else {
            CupFile.Wpt w = new CupFile.Wpt(Tr.t("Punkt ", "Point ") + (pts.size() + 1), lat, lon);
            pts.add(w);
        }
        changed();
    }

    @Override
    public void onPointDragStart(int idx) {
        snapshot();
    }

    @Override
    public void onPointMoved(int idx) {
        changed();
    }

    private void editPoint(int i) {
        CupFile.Wpt w = pts.get(i);
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(dp(16), dp(8), dp(16), 0);
        EditText nm = new EditText(this);
        nm.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_WORDS);
        nm.setText(w.name);
        nm.setSelectAllOnFocus(true);
        l.addView(nm);
        CheckBox land = new CheckBox(this);
        land.setText(Tr.t("Lądowisko", "Landing field"));
        land.setChecked(w.isLanding());
        l.addView(land);
        TextView pos = new TextView(this);
        pos.setText(CupFile.formatCoord(w.lat, false) + "  " + CupFile.formatCoord(w.lon, true)
                + (w.elev != null ? String.format(Locale.ROOT, "  %.0f m", w.elev) : "")
                + Tr.t("\nPrzesuwanie: przytrzymaj punkt na mapie i przeciągnij.", "\nTo move it: hold the point on the map and drag."));
        l.addView(pos);
        new AlertDialog.Builder(this)
                .setTitle(Tr.t("Punkt ", "Point ") + (i + 1))
                .setView(l)
                .setPositiveButton("OK", (d, x) -> {
                    snapshot();
                    w.name = nm.getText().toString().trim().isEmpty() ? w.name : nm.getText().toString().trim();
                    w.code = "";
                    w.style = land.isChecked() ? 3 : 1;
                    changed();
                })
                .setNegativeButton(Tr.t("Usuń", "Delete"), (d, x) -> {
                    snapshot();
                    pts.remove(i);
                    changed();
                })
                .setNeutralButton(Tr.t("Wstaw przed", "Insert before"), (d, x) -> {
                    snapshot();
                    CupFile.Wpt p;
                    if (i > 0) {
                        CupFile.Wpt a = pts.get(i - 1);
                        p = new CupFile.Wpt(Tr.t("Punkt ", "Point ") + (pts.size() + 1), (a.lat + w.lat) / 2, (a.lon + w.lon) / 2);
                    } else {
                        p = new CupFile.Wpt(Tr.t("Punkt ", "Point ") + (pts.size() + 1), w.lat + 0.01, w.lon);
                    }
                    pts.add(i, p);
                    changed();
                })
                .show();
    }

    private void rename() {
        EditText e = new EditText(this);
        e.setText(name);
        e.setSelectAllOnFocus(true);
        new AlertDialog.Builder(this)
                .setTitle(Tr.t("Nazwa trasy", "Route name"))
                .setView(e)
                .setPositiveButton("OK", (d, w) -> {
                    String n = Store.safeName(e.getText().toString());
                    if (n.equals(name)) return;
                    if (Store.routeFile(this, n).exists()) {
                        toast(Tr.t("Trasa o tej nazwie już istnieje", "A route with this name already exists"));
                        return;
                    }
                    File old = Store.routeFile(this, name);
                    name = n;
                    save();
                    old.delete();
                    refresh();
                })
                .setNegativeButton(Tr.t("Anuluj", "Cancel"), null)
                .show();
    }

    // ------------------------------------------------------------------ list + summary
    private void refresh() {
        title.setText(name);
        double km = CupFile.length(pts) / 1000;
        float v = prefs.getFloat("cruiseKmh", 40f);
        int min = (int) Math.round(km / v * 60);
        summary.setText(String.format(Locale.ROOT, Tr.t("%d pkt · %.1f km · ~%d:%02d h przy %.0f km/h",
                "%d pts · %.1f km · ~%d:%02d h at %.0f km/h"), pts.size(), km, min / 60, min % 60, v));
        list.removeAllViews();
        for (int i = 0; i < pts.size(); i++) {
            CupFile.Wpt w = pts.get(i);
            String leg = "";
            if (i > 0) {
                CupFile.Wpt a = pts.get(i - 1);
                leg = String.format(Locale.ROOT, "%.1f km  %03.0f°", Geo.distance(a.lat, a.lon, w.lat, w.lon) / 1000,
                        Geo.bearing(a.lat, a.lon, w.lat, w.lon));
            }
            LinearLayout row = new LinearLayout(this);
            row.setPadding(0, dp(6), 0, dp(6));
            TextView t1 = new TextView(this);
            t1.setText((i + 1) + (w.isLanding() ? " L  " : "    ") + w.name);
            t1.setTextSize(15);
            TextView t2 = new TextView(this);
            t2.setText(leg);
            t2.setTextSize(15);
            t2.setTypeface(Typeface.MONOSPACE);
            row.addView(t1, new LinearLayout.LayoutParams(0, -2, 1));
            row.addView(t2);
            int idx = i;
            row.setOnClickListener(x -> editPoint(idx));
            list.addView(row);
            View sep = new View(this);
            sep.setBackgroundColor(Color.LTGRAY);
            list.addView(sep, new LinearLayout.LayoutParams(-1, 1));
        }
    }

    // ------------------------------------------------------------------ menu
    private void menu() {
        String[] items = {
                Tr.t("Zmień nazwę", "Rename"),
                Tr.t("Odwróć kolejność punktów", "Reverse the route"),
                Tr.t("Usuń wszystkie punkty", "Delete all points"),
                (map.showAirspace() ? Tr.t("Ukryj strefy", "Hide airspace") : Tr.t("Pokaż strefy", "Show airspace")),
                (map.showWaypoints() ? Tr.t("Ukryj punkty", "Hide waypoints") : Tr.t("Pokaż punkty", "Show waypoints")),
                Tr.t("Dodaj strefy na mapę (OpenAir .txt)…", "Add airspace to the map (OpenAir .txt)…"),
                Tr.t("Dodaj punkty na mapę (.cup)…", "Add waypoints to the map (.cup)…"),
                Tr.t("Usuń strefy i punkty z mapy", "Remove airspace and waypoints from the map"),
                Tr.t("Prędkość przelotowa (czas trasy)", "Cruise speed (route time)"),
                Tr.t("Eksportuj plik .cup…", "Export .cup file…"),
        };
        new AlertDialog.Builder(this).setItems(items, (d, which) -> {
            switch (which) {
                case 0: rename(); break;
                case 1: snapshot(); java.util.Collections.reverse(pts); changed(); break;
                case 2:
                    new AlertDialog.Builder(this).setMessage(Tr.t("Usunąć wszystkie punkty trasy?", "Delete all route points?"))
                            .setPositiveButton(Tr.t("Usuń", "Delete"), (d2, w2) -> {
                                snapshot();
                                pts.clear();
                                changed();
                            })
                            .setNegativeButton(Tr.t("Anuluj", "Cancel"), null).show();
                    break;
                case 3:
                    map.setShowAirspace(!map.showAirspace());
                    prefs.edit().putBoolean("showAirspace", map.showAirspace()).apply();
                    break;
                case 4:
                    map.setShowWaypoints(!map.showWaypoints());
                    prefs.edit().putBoolean("showWaypoints", map.showWaypoints()).apply();
                    break;
                case 5: pick(REQ_AIRSPACE); break;
                case 6: pick(REQ_WAYPOINTS); break;
                case 7: Store.clearOverlays(this); loadOverlays(); break;
                case 8: cruiseSpeed(); break;
                case 9: export(); break;
                default: break;
            }
        }).show();
    }

    private void cruiseSpeed() {
        EditText e = new EditText(this);
        e.setInputType(InputType.TYPE_CLASS_NUMBER);
        e.setText(String.valueOf((int) prefs.getFloat("cruiseKmh", 40f)));
        new AlertDialog.Builder(this).setTitle(Tr.t("Prędkość przelotowa [km/h]", "Cruise speed [km/h]")).setView(e)
                .setPositiveButton("OK", (d, w) -> {
                    try {
                        float v = Float.parseFloat(e.getText().toString());
                        if (v >= 5 && v <= 200) prefs.edit().putFloat("cruiseKmh", v).apply();
                    } catch (NumberFormatException ignored) {
                        // keep the old value
                    }
                    refresh();
                }).setNegativeButton(Tr.t("Anuluj", "Cancel"), null).show();
    }

    private void pick(int req) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, req);
    }

    private void export() {
        save();
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("application/octet-stream");
        i.putExtra(Intent.EXTRA_TITLE, name + ".cup");
        startActivityForResult(i, REQ_EXPORT);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            if (req == REQ_EXPORT) {
                try (OutputStream o = getContentResolver().openOutputStream(uri)) {
                    if (o == null) throw new IOException("?");
                    o.write(CupFile.write(pts).getBytes(StandardCharsets.UTF_8));
                }
                toast(Tr.t("Wyeksportowano", "Exported"));
            } else {
                byte[] b = MainActivity.readAll(this, uri);
                String n = MainActivity.displayName(this, uri);
                boolean air = req == REQ_AIRSPACE;
                String warn = MainActivity.checkContent(air ? Proto.KIND_AIRSPACE : Proto.KIND_WAYPOINTS, n, b);
                if (warn != null) {
                    toast(warn);
                    return;
                }
                Store.saveOverlay(this, air ? Store.OVL_AIRSPACE : Store.OVL_WAYPOINTS, n, b, false);
                if (air) {
                    map.setShowAirspace(true);
                    prefs.edit().putBoolean("showAirspace", true).apply();
                } else {
                    map.setShowWaypoints(true);
                    prefs.edit().putBoolean("showWaypoints", true).apply();
                }
                loadOverlays();
            }
        } catch (IOException e) {
            toast(Tr.t("Błąd: ", "Error: ") + e.getMessage());
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        prefs.edit().putFloat("lat", (float) map.centerLat()).putFloat("lon", (float) map.centerLon())
                .putFloat("zoom", (float) map.zoom()).apply();
        if (!pts.isEmpty()) save();
    }

    @Override
    protected void onDestroy() {
        // an empty new route is not kept
        if (pts.isEmpty()) Store.routeFile(this, name).delete();
        map.shutdown();
        super.onDestroy();
    }
}
