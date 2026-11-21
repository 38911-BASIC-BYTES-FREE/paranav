package pl.paranav.uploader;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/** "My routes": routes planned / imported on the phone. */
public class RoutesActivity extends Activity {
    private static final int REQ_IMPORT = 1, REQ_EXPORT = 2;
    private LinearLayout list;
    private File exporting;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        int pad = dp(16);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);
        TextView t = new TextView(this);
        t.setText(Tr.t("Moje trasy", "My routes"));
        t.setTextSize(22);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(t);
        LinearLayout bar = new LinearLayout(this);
        Button nw = new Button(this);
        nw.setAllCaps(false);
        nw.setText(Tr.t("Nowa trasa", "New route"));
        nw.setOnClickListener(v -> startActivity(new Intent(this, PlannerActivity.class)));
        Button imp = new Button(this);
        imp.setAllCaps(false);
        imp.setText(Tr.t("Importuj .cup", "Import .cup"));
        imp.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_IMPORT);
        });
        bar.addView(nw, new LinearLayout.LayoutParams(0, -2, 1));
        bar.addView(imp, new LinearLayout.LayoutParams(0, -2, 1));
        root.addView(bar);
        ScrollView sv = new ScrollView(this);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        sv.addView(list);
        root.addView(sv, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }

    private void refresh() {
        list.removeAllViews();
        List<File> fs = Store.routes(this);
        if (fs.isEmpty()) {
            TextView e = new TextView(this);
            e.setText(Tr.t("Brak tras. Naciśnij „Nowa trasa” albo zaimportuj plik .cup.",
                    "No routes yet. Press \"New route\" or import a .cup file."));
            e.setPadding(0, dp(16), 0, 0);
            list.addView(e);
        }
        SimpleDateFormat df = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.ROOT);
        for (File f : fs) {
            List<CupFile.Wpt> pts;
            try {
                pts = Store.loadRoute(f);
            } catch (IOException e) {
                continue;
            }
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(0, dp(10), 0, dp(10));
            TextView n = new TextView(this);
            n.setText(Store.routeName(f));
            n.setTextSize(17);
            n.setTypeface(Typeface.DEFAULT_BOLD);
            TextView d = new TextView(this);
            d.setText(String.format(Locale.ROOT, Tr.t("%d pkt · %.1f km · %s", "%d pts · %.1f km · %s"), pts.size(),
                    CupFile.length(pts) / 1000, df.format(new Date(f.lastModified()))));
            row.addView(n);
            row.addView(d);
            row.setOnClickListener(v -> actions(f, pts));
            list.addView(row);
            View sep = new View(this);
            sep.setBackgroundColor(Color.LTGRAY);
            list.addView(sep, new LinearLayout.LayoutParams(-1, 1));
        }
    }

    private void actions(File f, List<CupFile.Wpt> pts) {
        String name = Store.routeName(f);
        String[] items = {Tr.t("Edytuj na mapie", "Edit on the map"), Tr.t("Wyślij do ParaNav", "Send to ParaNav"),
                Tr.t("Duplikuj", "Duplicate"), Tr.t("Zmień nazwę", "Rename"), Tr.t("Eksportuj plik .cup…", "Export .cup file…"),
                Tr.t("Usuń", "Delete")};
        new AlertDialog.Builder(this).setTitle(name).setItems(items, (d, w) -> {
            switch (w) {
                case 0:
                    startActivity(new Intent(this, PlannerActivity.class).putExtra(PlannerActivity.EXTRA_ROUTE, name));
                    break;
                case 1: RouteSender.send(this, name, pts); break;
                case 2: {
                    String n = Store.uniqueRouteName(this, name);
                    try {
                        Store.write(Store.routeFile(this, n), Store.read(f));
                    } catch (IOException e) {
                        toast(e.getMessage());
                    }
                    refresh();
                    break;
                }
                case 3: rename(f); break;
                case 4: {
                    exporting = f;
                    Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                    i.addCategory(Intent.CATEGORY_OPENABLE);
                    i.setType("application/octet-stream");
                    i.putExtra(Intent.EXTRA_TITLE, f.getName());
                    startActivityForResult(i, REQ_EXPORT);
                    break;
                }
                case 5:
                    new AlertDialog.Builder(this).setMessage(Tr.t("Usunąć trasę „", "Delete route \"") + name
                                    + Tr.t("” z telefonu?", "\" from the phone?"))
                            .setPositiveButton(Tr.t("Usuń", "Delete"), (d2, w2) -> {
                                f.delete();
                                refresh();
                            })
                            .setNegativeButton(Tr.t("Anuluj", "Cancel"), null).show();
                    break;
                default: break;
            }
        }).show();
    }

    private void rename(File f) {
        EditText e = new EditText(this);
        e.setText(Store.routeName(f));
        e.setSelectAllOnFocus(true);
        new AlertDialog.Builder(this).setTitle(Tr.t("Nazwa trasy", "Route name")).setView(e)
                .setPositiveButton("OK", (d, w) -> {
                    File to = Store.routeFile(this, e.getText().toString());
                    if (to.exists()) toast(Tr.t("Trasa o tej nazwie już istnieje", "A route with this name already exists"));
                    else if (!f.renameTo(to)) toast(Tr.t("Nie udało się", "Failed"));
                    refresh();
                })
                .setNegativeButton(Tr.t("Anuluj", "Cancel"), null).show();
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            if (req == REQ_IMPORT) {
                byte[] b = MainActivity.readAll(this, uri);
                List<CupFile.Wpt> pts = CupFile.parse(Store.decode(b));
                if (pts.isEmpty()) {
                    toast(Tr.t("W pliku nie ma punktów (.cup)", "No waypoints in the file (.cup)"));
                    return;
                }
                String n = MainActivity.displayName(this, uri);
                if (n.toLowerCase(Locale.ROOT).endsWith(".cup")) n = n.substring(0, n.length() - 4);
                n = Store.uniqueRouteName(this, n);
                Store.write(Store.routeFile(this, n), CupFile.write(pts).getBytes(StandardCharsets.UTF_8));
                toast(Tr.t("Zaimportowano: ", "Imported: ") + n + " (" + pts.size() + ")");
                refresh();
            } else if (req == REQ_EXPORT && exporting != null) {
                try (OutputStream o = getContentResolver().openOutputStream(uri)) {
                    if (o == null) throw new IOException("?");
                    o.write(Store.read(exporting));
                }
                toast(Tr.t("Wyeksportowano", "Exported"));
            }
        } catch (IOException e) {
            toast(Tr.t("Błąd: ", "Error: ") + e.getMessage());
        }
    }
}
