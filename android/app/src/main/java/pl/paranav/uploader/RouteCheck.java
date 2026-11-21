package pl.paranav.uploader;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Airspace crossed by a planned route (the app's version of nav::checkRoute in lib/navcore/src/escape.cpp).
 * The phone does not know the terrain or today's activation schedule, so the floor is estimated
 * conservatively (MSL counted as above ground) and time-limited zones are reported anyway -
 * ParaNav repeats the check with its terrain and schedule when the route is activated.
 */
final class RouteCheck {
    private RouteCheck() {}

    /** Same default as the firmware setting routeCheckAglM. */
    static final double CHECK_AGL_M = 1000;
    /** Classes ParaNav ignores by default (Settings::asIgnore). */
    private static final String[] IGNORED = {"G", "W", "FIR", "UIR", "GP"};

    static final class Hit {
        OpenAir.Zone zone;
        double km;          // from the route start to the first crossing
        double floorM;      // estimated floor above ground (0 = from the ground)
        boolean conflict;   // floor below CHECK_AGL_M
    }

    static boolean ignored(OpenAir.Zone z) {
        for (String c : IGNORED)
            if (c.equalsIgnoreCase(z.cls)) return true;
        return false;
    }

    /** Floor in metres, AMSL counted as above ground (conservative); NaN if unreadable. */
    static double floorMetres(String s) {
        String u = s.trim().toUpperCase(Locale.ROOT);
        if (u.isEmpty() || u.startsWith("GND") || u.startsWith("SFC") || u.startsWith("SURFACE")) return 0;
        if (u.startsWith("UNL")) return 99999;
        try {
            if (u.startsWith("FL")) return Double.parseDouble(u.substring(2).trim().split("[^0-9.]")[0]) * 100 * 0.3048;
            int i = 0;
            while (i < u.length() && (Character.isDigit(u.charAt(i)) || u.charAt(i) == '.')) i++;
            if (i == 0) return Double.NaN;
            double v = Double.parseDouble(u.substring(0, i));
            String rest = u.substring(i).trim();
            boolean metres = rest.startsWith("M") && !rest.startsWith("MSL");
            return metres ? v : v * 0.3048;
        } catch (NumberFormatException e) {
            return Double.NaN;
        }
    }

    static boolean contains(OpenAir.Zone z, double lat, double lon) {
        if (lat < z.minLat || lat > z.maxLat || lon < z.minLon || lon > z.maxLon) return false;
        boolean in = false;
        int n = z.pts.size();
        for (int k = 0, j = n - 1; k < n; j = k++) {
            double[] a = z.pts.get(k), b = z.pts.get(j);
            if ((a[0] > lat) != (b[0] > lat) && lon < (b[1] - a[1]) * (lat - a[0]) / (b[0] - a[0]) + a[1]) in = !in;
        }
        return in;
    }

    /** Zones crossed by the route, each once, in route order. */
    static List<Hit> check(List<OpenAir.Zone> zones, List<CupFile.Wpt> pts) {
        List<Hit> out = new ArrayList<>();
        if (zones == null) return out;
        boolean[] seen = new boolean[zones.size()];
        double start = 0;
        for (int leg = 0; leg + 1 < pts.size(); leg++) {
            CupFile.Wpt a = pts.get(leg), b = pts.get(leg + 1);
            double len = Geo.distance(a.lat, a.lon, b.lat, b.lon);
            double lo1 = Math.min(a.lat, b.lat), hi1 = Math.max(a.lat, b.lat);
            double lo2 = Math.min(a.lon, b.lon), hi2 = Math.max(a.lon, b.lon);
            int steps = Math.max(1, (int) (len / 100));
            for (int zi = 0; zi < zones.size(); zi++) {
                if (seen[zi]) continue;
                OpenAir.Zone z = zones.get(zi);
                if (z.pts.size() < 3 || ignored(z)) continue;
                if (z.maxLat < lo1 || z.minLat > hi1 || z.maxLon < lo2 || z.minLon > hi2) continue;
                for (int s = 0; s <= steps; s++) {
                    double f = (double) s / steps;
                    double lat = a.lat + (b.lat - a.lat) * f, lon = a.lon + (b.lon - a.lon) * f;
                    if (!contains(z, lat, lon)) continue;
                    Hit h = new Hit();
                    h.zone = z;
                    h.km = (start + len * f) / 1000;
                    h.floorM = floorMetres(z.lower);
                    h.conflict = Double.isNaN(h.floorM) || h.floorM < CHECK_AGL_M;
                    out.add(h);
                    seen[zi] = true;
                    break;
                }
            }
            start += len;
        }
        out.sort((x, y) -> Double.compare(x.km, y.km));
        return out;
    }

    /** Short text for a dialog; empty when nothing below the check height is crossed. */
    static String summary(List<Hit> hits) {
        StringBuilder sb = new StringBuilder();
        int n = 0;
        for (Hit h : hits) {
            if (!h.conflict) continue;
            if (n < 8)
                sb.append(String.format(Locale.ROOT, "• %s  (%s – %s)  %.1f km\n", h.zone.name, h.zone.lower, h.zone.upper, h.km));
            n++;
        }
        if (n == 0) return "";
        if (n > 8) sb.append(Tr.t("… i jeszcze ", "… and ")).append(n - 8).append(Tr.t("\n", " more\n"));
        return String.format(Locale.ROOT, Tr.t("Trasa przecina %d stref(y) z podstawą poniżej %d m:\n", "The route crosses %d airspace zone(s) with a floor below %d m:\n"),
                n, (int) CHECK_AGL_M) + sb
                + Tr.t("Strefy czasowe (TRA/TSA/D/R) mogą być dziś nieaktywne – ParaNav sprawdzi to wg planu aktywności.",
                        "Time-limited zones (TRA/TSA/D/R) may be inactive today – ParaNav checks the activation schedule.");
    }
}
