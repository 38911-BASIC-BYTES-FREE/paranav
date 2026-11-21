package pl.paranav.uploader;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Files kept on the phone: planned routes and the airspace / waypoint files shown on the map. */
final class Store {
    private Store() {}

    static final String ROUTES = "routes", OVL_AIRSPACE = "overlay/airspace", OVL_WAYPOINTS = "overlay/waypoints";

    static File dir(Context c, String sub) {
        File d = new File(c.getFilesDir(), sub);
        if (!d.isDirectory()) d.mkdirs();
        return d;
    }

    /** Text files from other programs may be UTF-8 or Windows-1250 (Polish OpenAir files often are). */
    static String decode(byte[] b) {
        try {
            return StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(b)).toString();
        } catch (CharacterCodingException e) {
            return new String(b, Charset.forName("windows-1250"));
        }
    }

    static byte[] read(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        }
    }

    static void write(File f, byte[] data) throws IOException {
        File tmp = new File(f.getPath() + ".tmp");
        try (FileOutputStream o = new FileOutputStream(tmp)) {
            o.write(data);
        }
        if (!tmp.renameTo(f)) {
            f.delete();
            if (!tmp.renameTo(f)) throw new IOException("rename " + f.getName());
        }
    }

    /** File-system safe name (letters incl. Polish, digits, space, - _ .). */
    static String safeName(String s) {
        String r = s.trim().replaceAll("[\\\\/:*?\"<>|\\n\\r\\t]", "_");
        if (r.isEmpty()) r = "trasa";
        return r.length() > 40 ? r.substring(0, 40) : r;
    }

    // ------------------------------------------------------------------ routes
    static File routeFile(Context c, String name) {
        return new File(dir(c, ROUTES), safeName(name) + ".cup");
    }

    /** Routes, newest first. */
    static List<File> routes(Context c) {
        File[] fs = dir(c, ROUTES).listFiles((d, n) -> n.toLowerCase().endsWith(".cup"));
        List<File> l = new ArrayList<>(fs == null ? new ArrayList<File>() : Arrays.asList(fs));
        l.sort((a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        return l;
    }

    static String routeName(File f) {
        String n = f.getName();
        return n.toLowerCase().endsWith(".cup") ? n.substring(0, n.length() - 4) : n;
    }

    static List<CupFile.Wpt> loadRoute(File f) throws IOException {
        return CupFile.parse(decode(read(f)));
    }

    /** Name not used by another route yet: "Trasa", "Trasa 2", ... */
    static String uniqueRouteName(Context c, String base) {
        String n = safeName(base);
        if (!routeFile(c, n).exists()) return n;
        for (int i = 2; ; i++)
            if (!routeFile(c, n + " " + i).exists()) return n + " " + i;
    }

    // ------------------------------------------------------------------ map overlays
    static void saveOverlay(Context c, String sub, String name, byte[] data, boolean replace) throws IOException {
        File d = dir(c, sub);
        if (replace) {
            File[] old = d.listFiles();
            if (old != null) for (File f : old) f.delete();
        }
        write(new File(d, safeName(name)), data);
    }

    static void clearOverlays(Context c) {
        for (String sub : new String[] {OVL_AIRSPACE, OVL_WAYPOINTS}) {
            File[] old = dir(c, sub).listFiles();
            if (old != null) for (File f : old) f.delete();
        }
    }

    static List<OpenAir.Zone> loadAirspace(Context c) {
        List<OpenAir.Zone> out = new ArrayList<>();
        File[] fs = dir(c, OVL_AIRSPACE).listFiles();
        if (fs != null)
            for (File f : fs) {
                try {
                    out.addAll(OpenAir.parse(decode(read(f))));
                } catch (IOException ignored) {
                    // unreadable file: skip
                }
            }
        return out;
    }

    static List<CupFile.Wpt> loadWaypoints(Context c) {
        List<CupFile.Wpt> out = new ArrayList<>();
        File[] fs = dir(c, OVL_WAYPOINTS).listFiles();
        if (fs != null)
            for (File f : fs) {
                try {
                    out.addAll(CupFile.parse(decode(read(f))));
                } catch (IOException ignored) {
                    // unreadable file: skip
                }
            }
        return out;
    }
}
