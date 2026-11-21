package pl.paranav.uploader;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;
import java.util.regex.Pattern;

/** Main screen: connect to ParaNav, send airspace / waypoint / route / activation files, open the route planner. */
public class MainActivity extends Activity {
    private static final int REQ_PERMS = 1;
    private static final int REQ_BT_ON = 2;
    private static final int REQ_FILE_AIRSPACE = 10, REQ_FILE_WAYPOINTS = 11, REQ_FILE_ROUTE = 12;

    private BluetoothAdapter adapter;

    private TextView status, log;
    private Button connectBtn;
    private ProgressBar progress;
    private CheckBox replAirspace, replWaypoints, replRoute;
    private EditText activations;
    private final List<View> needConnection = new ArrayList<>();

    // ------------------------------------------------------------------ UI
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        BluetoothManager bm = (BluetoothManager) getSystemService(BLUETOOTH_SERVICE);
        adapter = bm != null ? bm.getAdapter() : null;

        int pad = dp(16);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);

        TextView title = text(Tr.t("ParaNav – przesyłanie plików", "ParaNav – file upload"), 22, true);
        root.addView(title);
        status = text(Tr.t("Nie połączono", "Not connected"), 16, false);
        root.addView(status);
        connectBtn = button(Tr.t("Połącz z ParaNav", "Connect to ParaNav"), v -> toggleConnection());
        root.addView(connectBtn);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        root.addView(progress);

        root.addView(section(Tr.t("Trasy", "Routes")));
        root.addView(text(Tr.t("Planowanie na mapie (OpenTopoMap), ze strefami i punktami z plików wysłanych do ParaNav.",
                "Planning on the map (OpenTopoMap), with the airspace and waypoints from the files sent to ParaNav."), 13, false));
        root.addView(button(Tr.t("Zaplanuj nową trasę na mapie", "Plan a new route on the map"),
                v -> startActivity(new Intent(this, PlannerActivity.class))));
        root.addView(button(Tr.t("Moje trasy (edytuj, wyślij, eksportuj)", "My routes (edit, send, export)"),
                v -> startActivity(new Intent(this, RoutesActivity.class))));

        root.addView(section(Tr.t("Strefy (OpenAir, plik .txt)", "Airspace (OpenAir, .txt file)")));
        replAirspace = check(Tr.t("Zastąp poprzednie strefy", "Replace previous airspace"), true);
        root.addView(replAirspace);
        root.addView(guarded(button(Tr.t("Wybierz plik stref i wyślij", "Choose airspace file and send"), v -> pick(REQ_FILE_AIRSPACE))));

        root.addView(section(Tr.t("Aktywacje stref na dziś (AUP)", "Today's airspace activations (AUP)")));
        root.addView(text(Tr.t("Jedna strefa w linii, czas UTC, np.  EPTR12 0800-1500  (kilka przedziałów po spacji).", "One airspace per line, UTC, e.g.  EPTR12 0800-1500  (several intervals separated by spaces)."), 13, false));
        activations = new EditText(this);
        activations.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS);
        activations.setMinLines(4);
        activations.setGravity(Gravity.TOP);
        activations.setTypeface(Typeface.MONOSPACE);
        activations.setHint("EPTR12 0800-1500");
        root.addView(activations);
        root.addView(guarded(button(Tr.t("Wyślij aktywacje", "Send activations"), v -> sendActivations())));

        root.addView(section(Tr.t("Punkty (SeeYou .cup)", "Waypoints (SeeYou .cup)")));
        replWaypoints = check(Tr.t("Zastąp poprzednie punkty", "Replace previous waypoints"), false);
        root.addView(replWaypoints);
        root.addView(guarded(button(Tr.t("Wybierz plik punktów i wyślij", "Choose waypoint file and send"), v -> pick(REQ_FILE_WAYPOINTS))));

        root.addView(section(Tr.t("Gotowy plik trasy (SeeYou .cup, kolejność punktów = trasa)", "Ready-made route file (SeeYou .cup, point order = route)")));
        replRoute = check(Tr.t("Usuń inne trasy z ParaNav", "Delete other routes on ParaNav"), false);
        root.addView(replRoute);
        root.addView(guarded(button(Tr.t("Wybierz plik trasy i wyślij", "Choose route file and send"), v -> pick(REQ_FILE_ROUTE))));

        root.addView(section(Tr.t("Pliki w urządzeniu", "Files on the device")));
        root.addView(guarded(button(Tr.t("Pokaż / usuń pliki", "Show / delete files"), v -> listFiles())));

        root.addView(section(Tr.t("Dziennik", "Log")));
        log = text("", 13, false);
        log.setTypeface(Typeface.MONOSPACE);
        root.addView(log);

        ScrollView sv = new ScrollView(this);
        sv.addView(root);
        setContentView(sv);
        setConnected(false);
        ensurePermissions();
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private TextView text(String s, int sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setPadding(0, dp(4), 0, dp(4));
        return t;
    }

    private TextView section(String s) {
        TextView t = text(s, 17, true);
        t.setPadding(0, dp(18), 0, dp(4));
        return t;
    }

    private Button button(String s, View.OnClickListener l) {
        Button btn = new Button(this);
        btn.setText(s);
        btn.setAllCaps(false);
        btn.setOnClickListener(l);
        return btn;
    }

    private CheckBox check(String s, boolean on) {
        CheckBox c = new CheckBox(this);
        c.setText(s);
        c.setChecked(on);
        return c;
    }

    private View guarded(View v) {
        needConnection.add(v);
        return v;
    }

    private void logLine(String s) {
        runOnUiThread(() -> {
            String t = new SimpleDateFormat("HH:mm:ss", Locale.ROOT).format(new Date());
            log.setText(t + "  " + s + "\n" + log.getText());
        });
    }

    private void setStatus(String s) {
        runOnUiThread(() -> status.setText(s));
    }

    private void setProgress(long done, long total) {
        runOnUiThread(() -> progress.setProgress(total > 0 ? (int) (done * 1000 / total) : 0));
    }

    private void setConnected(boolean c) {
        runOnUiThread(() -> {
            connectBtn.setText(c ? Tr.t("Rozłącz", "Disconnect") : Tr.t("Połącz z ParaNav", "Connect to ParaNav"));
            for (View v : needConnection) v.setEnabled(c);
            if (!c) status.setText(Tr.t("Nie połączono", "Not connected"));
        });
    }

    private void setBusy(boolean busy) {
        runOnUiThread(() -> {
            connectBtn.setEnabled(!busy);
            boolean c = Conn.connected();
            for (View v : needConnection) v.setEnabled(!busy && c);
        });
    }

    // ------------------------------------------------------------------ permissions
    private String[] neededPermissions() {
        if (Build.VERSION.SDK_INT >= 31)
            return new String[] {Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT};
        return new String[] {Manifest.permission.ACCESS_FINE_LOCATION};
    }

    private boolean ensurePermissions() {
        List<String> missing = new ArrayList<>();
        for (String p : neededPermissions())
            if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) missing.add(p);
        if (missing.isEmpty()) return true;
        requestPermissions(missing.toArray(new String[0]), REQ_PERMS);
        return false;
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        for (int r : results)
            if (r != PackageManager.PERMISSION_GRANTED) {
                logLine(Tr.t("Bez uprawnień Bluetooth aplikacja nie połączy się z ParaNav.", "Without Bluetooth permissions the app cannot connect to ParaNav."));
                return;
            }
    }

    // ------------------------------------------------------------------ connection
    private void toggleConnection() {
        if (Conn.connected()) {
            Conn.worker.execute(() -> {
                Conn.close();
                setConnected(false);
                logLine(Tr.t("Rozłączono.", "Disconnected."));
            });
            return;
        }
        if (adapter == null) {
            logLine(Tr.t("Ten telefon nie ma Bluetooth.", "This phone has no Bluetooth."));
            return;
        }
        if (!ensurePermissions()) return;
        if (!adapter.isEnabled()) {
            try {
                startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQ_BT_ON);
            } catch (SecurityException e) {
                logLine(Tr.t("Włącz Bluetooth w ustawieniach telefonu.", "Turn on Bluetooth in the phone settings."));
            }
            return;
        }
        setBusy(true);
        Conn.worker.execute(() -> {
            BleLink link = null;
            try {
                setStatus(Tr.t("Szukam ParaNav…", "Looking for ParaNav…"));
                link = new BleLink(this, adapter, () -> {
                    setConnected(false);
                    logLine(Tr.t("Połączenie przerwane.", "Connection lost."));
                });
                BluetoothDevice d = link.scan(12_000);
                setStatus(Tr.t("Łączę…", "Connecting…"));
                link.connect(d);
                Uploader uploader = new Uploader(link);
                String hello = uploader.hello();
                Conn.link = link;
                Conn.uploader = uploader;
                Conn.banner = hello;
                setStatus(Tr.t("Połączono: ", "Connected: ") + hello.replace("PARANAV 1", "ParaNav, firmware"));
                logLine(Tr.t("Połączono (pakiet ", "Connected (packet ") + link.maxPacket() + " B).");
                setConnected(true);
            } catch (Exception e) {
                if (link != null) link.close();
                setConnected(false);
                logLine(Tr.t("Błąd połączenia: ", "Connection error: ") + e.getMessage());
            } finally {
                setBusy(false);
            }
        });
    }

    // ------------------------------------------------------------------ files
    private void pick(int req) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");  // OpenAir .txt and SeeYou .cup have no reliable MIME types
        startActivityForResult(i, req);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_BT_ON) {
            if (res == RESULT_OK) toggleConnection();
            return;
        }
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        String kind;
        boolean replace;
        switch (req) {
            case REQ_FILE_AIRSPACE: kind = Proto.KIND_AIRSPACE; replace = replAirspace.isChecked(); break;
            case REQ_FILE_WAYPOINTS: kind = Proto.KIND_WAYPOINTS; replace = replWaypoints.isChecked(); break;
            case REQ_FILE_ROUTE: kind = Proto.KIND_ROUTE; replace = replRoute.isChecked(); break;
            default: return;
        }
        try {
            String name = displayName(this, uri);
            byte[] bytes = readAll(this, uri);
            String warn = checkContent(kind, name, bytes);
            if (warn != null) {
                new AlertDialog.Builder(this)
                        .setTitle(Tr.t("Czy to właściwy plik?", "Is this the right file?"))
                        .setMessage(warn)
                        .setPositiveButton(Tr.t("Wyślij mimo to", "Send anyway"), (d, w) -> send(kind, name, bytes, replace))
                        .setNegativeButton(Tr.t("Anuluj", "Cancel"), null)
                        .show();
            } else {
                send(kind, name, bytes, replace);
            }
        } catch (IOException e) {
            logLine(Tr.t("Nie mogę odczytać pliku: ", "Cannot read the file: ") + e.getMessage());
        }
    }

    static String displayName(android.content.Context ctx, Uri uri) {
        try (Cursor c = ctx.getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        }
        String s = uri.getLastPathSegment();
        return s != null ? s : Tr.t("plik", "file");
    }

    static byte[] readAll(android.content.Context ctx, Uri uri) throws IOException {
        try (InputStream in = ctx.getContentResolver().openInputStream(uri)) {
            if (in == null) throw new IOException(Tr.t("brak danych", "no data"));
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                if (out.size() > Proto.MAX_FILE_SIZE) throw new IOException(Tr.t("plik większy niż 8 MB", "file larger than 8 MB"));
            }
            return out.toByteArray();
        }
    }

    /** Quick sanity check on the phone so an obviously wrong file is not sent. Returns a warning or null. */
    static String checkContent(String kind, String name, byte[] bytes) {
        String head = new String(bytes, 0, Math.min(bytes.length, 20000), StandardCharsets.ISO_8859_1);
        if (Proto.KIND_AIRSPACE.equals(kind)) {
            if (!Pattern.compile("(?m)^\\s*AC\\s").matcher(head).find())
                return name + Tr.t(" nie wygląda na plik OpenAir (brak linii \"AC ...\").", " does not look like an OpenAir file (no \"AC ...\" lines).");
        } else {
            if (!Pattern.compile("\\d{4}\\.\\d{3}[NS]\\s*,\\s*\\d{5}\\.\\d{3}[EW]").matcher(head).find())
                return name + Tr.t(" nie wygląda na plik SeeYou .cup (brak współrzędnych typu 5212.345N,02101.234E).", " does not look like a SeeYou .cup file (no coordinates like 5212.345N,02101.234E).");
        }
        return null;
    }

    private void send(String kind, String name, byte[] bytes, boolean replace) {
        setBusy(true);
        logLine(Tr.t("Wysyłam ", "Sending ") + name + " (" + (bytes.length / 1024) + " kB)…");
        // keep a copy of airspace / waypoints for the map in the route planner
        if (Proto.KIND_AIRSPACE.equals(kind) || Proto.KIND_WAYPOINTS.equals(kind)) {
            try {
                Store.saveOverlay(this, Proto.KIND_AIRSPACE.equals(kind) ? Store.OVL_AIRSPACE : Store.OVL_WAYPOINTS, name, bytes,
                        replace);
            } catch (IOException ignored) {
                // the map overlay is a convenience only
            }
        }
        Conn.worker.execute(() -> {
            long t0 = System.currentTimeMillis();
            try {
                String r = Conn.uploader.upload(kind, name, bytes, replace, this::setProgress);
                // "OK END <count> <path>"
                String[] p = r.split(" ", 4);
                String count = p.length > 2 ? p[2] : "?";
                String path = p.length > 3 ? p[3] : "";
                double s = (System.currentTimeMillis() - t0) / 1000.0;
                logLine(String.format(Locale.ROOT, "OK: %s – %s %s (%.0f s)", path, count, unit(kind), s));
            } catch (Exception e) {
                logLine(Tr.t("Błąd: ", "Error: ") + e.getMessage());
            } finally {
                setProgress(0, 0);
                setBusy(false);
            }
        });
    }

    private static String unit(String kind) {
        switch (kind) {
            case Proto.KIND_AIRSPACE: return Tr.t("stref", "airspaces");
            case Proto.KIND_ACTIVATIONS: return Tr.t("aktywacji", "activations");
            default: return Tr.t("punktów", "waypoints");
        }
    }

    // ------------------------------------------------------------------ activations
    private static final Pattern ACT_LINE = Pattern.compile("^[A-Z0-9_]+(\\s+\\d{4}-\\d{4})+$");

    private void sendActivations() {
        String[] lines = activations.getText().toString().toUpperCase(Locale.ROOT).split("\\r?\\n");
        StringBuilder sb = new StringBuilder();
        SimpleDateFormat df = new SimpleDateFormat("yyyy-MM-dd", Locale.ROOT);
        df.setTimeZone(TimeZone.getTimeZone("UTC"));
        sb.append(Tr.t("# z aplikacji ParaNav\n", "# from the ParaNav app\n")).append("date=").append(df.format(new Date())).append('\n');
        int n = 0;
        for (String l : lines) {
            String t = l.trim().replaceAll("\\s+", " ");
            if (t.isEmpty()) continue;
            if (!ACT_LINE.matcher(t).matches()) {
                logLine(Tr.t("Zła linia aktywacji: \"", "Bad activation line: \"") + t + Tr.t("\" – oczekiwane np. EPTR12 0800-1500", "\" – expected e.g. EPTR12 0800-1500"));
                return;
            }
            sb.append(t).append('\n');
            n++;
        }
        if (n == 0) {
            new AlertDialog.Builder(this)
                    .setMessage(Tr.t("Brak wpisów. Wysłać pustą listę? Strefy aktywowane dziennie będą wtedy traktowane ", "No entries. Send an empty list? Daily activated airspace will then be treated ")
                            + Tr.t("jako NIEAKTYWNE przez cały dzień.", "as INACTIVE for the whole day."))
                    .setPositiveButton(Tr.t("Wyślij", "Send"), (d, w) -> send(Proto.KIND_ACTIVATIONS, "activations.txt",
                            sb.toString().getBytes(StandardCharsets.UTF_8), true))
                    .setNegativeButton(Tr.t("Anuluj", "Cancel"), null)
                    .show();
            return;
        }
        send(Proto.KIND_ACTIVATIONS, "activations.txt", sb.toString().getBytes(StandardCharsets.UTF_8), true);
    }

    // ------------------------------------------------------------------ list / delete
    private void listFiles() {
        setBusy(true);
        Conn.worker.execute(() -> {
            try {
                List<String> files = Conn.uploader.list();
                runOnUiThread(() -> showFiles(files));
            } catch (Exception e) {
                logLine(Tr.t("Błąd: ", "Error: ") + e.getMessage());
            } finally {
                setBusy(false);
            }
        });
    }

    private void showFiles(List<String> files) {
        if (files.isEmpty()) {
            new AlertDialog.Builder(this).setMessage(Tr.t("Brak plików w urządzeniu.", "No files on the device.")).setPositiveButton("OK", null).show();
            return;
        }
        String[] items = new String[files.size()];
        for (int i = 0; i < items.length; i++) {
            String f = files.get(i);
            int sp = f.lastIndexOf(' ');
            long size = sp > 0 ? Long.parseLong(f.substring(sp + 1)) : 0;
            items[i] = (sp > 0 ? f.substring(0, sp) : f) + "  (" + Math.max(1, size / 1024) + " kB)";
        }
        new AlertDialog.Builder(this)
                .setTitle(Tr.t("Dotknij pliku, aby go usunąć", "Tap a file to delete it"))
                .setItems(items, (d, which) -> {
                    String f = files.get(which);
                    int sp = f.lastIndexOf(' ');
                    String path = sp > 0 ? f.substring(0, sp) : f;
                    new AlertDialog.Builder(this)
                            .setMessage(Tr.t("Usunąć ", "Delete ") + path + Tr.t(" z ParaNav?", " from ParaNav?"))
                            .setPositiveButton(Tr.t("Usuń", "Delete"), (d2, w2) -> delete(path))
                            .setNegativeButton(Tr.t("Anuluj", "Cancel"), null)
                            .show();
                })
                .setNegativeButton(Tr.t("Zamknij", "Close"), null)
                .show();
    }

    private void delete(String path) {
        setBusy(true);
        Conn.worker.execute(() -> {
            try {
                Conn.uploader.delete(path);
                logLine(Tr.t("Usunięto ", "Deleted ") + path);
            } catch (Exception e) {
                logLine(Tr.t("Błąd: ", "Error: ") + e.getMessage());
            } finally {
                setBusy(false);
            }
        });
    }

    @Override
    protected void onDestroy() {
        if (isFinishing()) Conn.close();
        super.onDestroy();
    }
}
