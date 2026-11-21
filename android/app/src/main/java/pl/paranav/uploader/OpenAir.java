package pl.paranav.uploader;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** OpenAir airspace parser for the map overlay (same rules as lib/navcore/src/airspace.cpp). */
final class OpenAir {
    private OpenAir() {}

    static final class Zone {
        String cls = "", name = "", type = "", lower = "", upper = "";
        final List<double[]> pts = new ArrayList<>();  // {lat, lon}
        double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;

        /** Prohibited / restricted / danger, controlled zones, reserved areas, others (for colours). */
        int category() {
            String n = name.toUpperCase(Locale.ROOT);
            if (cls.equals("P") || cls.equals("R") || cls.equals("Q") || n.startsWith("EPP") || n.startsWith("EPR")
                    || n.startsWith("EPD")) return 0;
            if (cls.equals("CTR") || type.equals("CTR") || n.contains("CTR")) return 1;
            if (type.equals("TRA") || type.equals("TSA") || n.startsWith("EPTR") || n.startsWith("EPTS")
                    || n.startsWith("TRA") || n.startsWith("TSA") || n.startsWith("MRT")) return 2;
            return 3;
        }
    }

    private static final double NM = 1852.0;

    /** "52:24:30.5", "52:24.5", "522430", "52.408" -> degrees */
    static double parseDms(String t, boolean lon) {
        t = t.trim();
        if (t.isEmpty()) return Double.NaN;
        try {
            if (t.contains(":")) {
                String[] p = t.split(":");
                double v = 0, f = 1;
                for (int i = 0; i < p.length && i < 3; i++, f *= 60) v += Double.parseDouble(p[i].trim()) / f;
                return v;
            }
            int dot = t.indexOf('.');
            int ip = dot >= 0 ? dot : t.length();
            int dd = lon ? 3 : 2;
            if (ip >= dd + 4)
                return Double.parseDouble(t.substring(0, ip - 4)) + Double.parseDouble(t.substring(ip - 4, ip - 2)) / 60
                        + Double.parseDouble(t.substring(ip - 2)) / 3600;
            if (ip >= dd + 2)
                return Double.parseDouble(t.substring(0, ip - 2)) + Double.parseDouble(t.substring(ip - 2)) / 60;
            return Double.parseDouble(t);
        } catch (NumberFormatException e) {
            return Double.NaN;
        }
    }

    /** "52:12:00 N 020:58:00 E" -> {lat, lon} or null */
    static double[] parseCoordPair(String s) {
        String u = s.toUpperCase(Locale.ROOT);
        int ns = -1;
        for (int i = 0; i < u.length(); i++)
            if (u.charAt(i) == 'N' || u.charAt(i) == 'S') { ns = i; break; }
        if (ns < 0) return null;
        int ew = -1;
        for (int i = ns + 1; i < u.length(); i++)
            if (u.charAt(i) == 'E' || u.charAt(i) == 'W') { ew = i; break; }
        if (ew < 0) return null;
        double la = parseDms(u.substring(0, ns), false), lo = parseDms(u.substring(ns + 1, ew), true);
        if (Double.isNaN(la) || Double.isNaN(lo)) return null;
        if (u.charAt(ns) == 'S') la = -la;
        if (u.charAt(ew) == 'W') lo = -lo;
        if (Math.abs(la) > 90 || Math.abs(lo) > 180) return null;
        return new double[] {la, lo};
    }

    private static void add(Zone z, double la, double lo) {
        if (!z.pts.isEmpty()) {
            double[] l = z.pts.get(z.pts.size() - 1);
            if (Math.abs(l[0] - la) < 1e-7 && Math.abs(l[1] - lo) < 1e-7) return;
        }
        z.pts.add(new double[] {la, lo});
    }

    private static void arc(Zone z, double cla, double clo, double r, double a0, double a1, boolean cw) {
        double span = cw ? ((a1 - a0) % 360 + 360) % 360 : -(((a0 - a1) % 360 + 360) % 360);
        if (Math.abs(span) < 1e-6) span = cw ? 360 : -360;
        int steps = Math.max(2, (int) Math.ceil(Math.abs(span) / 5.0));
        for (int i = 0; i <= steps; i++) {
            double[] p = Geo.destination(cla, clo, a0 + span * i / steps, r);
            add(z, p[0], p[1]);
        }
    }

    private static void finish(Zone z, List<Zone> out) {
        if (z == null) return;
        if (z.pts.size() > 1) {
            double[] f = z.pts.get(0), l = z.pts.get(z.pts.size() - 1);
            if (Math.abs(f[0] - l[0]) < 1e-7 && Math.abs(f[1] - l[1]) < 1e-7) z.pts.remove(z.pts.size() - 1);
        }
        if (z.pts.size() < 3) return;
        for (double[] p : z.pts) {
            z.minLat = Math.min(z.minLat, p[0]);
            z.maxLat = Math.max(z.maxLat, p[0]);
            z.minLon = Math.min(z.minLon, p[1]);
            z.maxLon = Math.max(z.maxLon, p[1]);
        }
        out.add(z);
    }

    static List<Zone> parse(String text) {
        List<Zone> out = new ArrayList<>();
        Zone z = null;
        double cx = 0, cy = 0;
        boolean cw = true;
        for (String raw : text.split("\\r?\\n")) {
            int star = raw.indexOf('*');
            String l = (star >= 0 ? raw.substring(0, star) : raw).trim();
            if (l.startsWith("﻿")) l = l.substring(1);
            if (l.length() < 2) continue;
            char k0 = Character.toUpperCase(l.charAt(0)), k1 = Character.toUpperCase(l.charAt(1));
            String arg = l.substring(2).trim();
            if (k0 == 'A' && k1 == 'C') {
                finish(z, out);
                z = new Zone();
                z.cls = arg.toUpperCase(Locale.ROOT);
                cw = true;
                continue;
            }
            if (z == null) continue;
            if (k0 == 'A') {
                if (k1 == 'N') z.name = arg;
                else if (k1 == 'Y') z.type = arg.toUpperCase(Locale.ROOT);
                else if (k1 == 'L') z.lower = arg;
                else if (k1 == 'H') z.upper = arg;
                continue;
            }
            if (k0 == 'V' && (l.charAt(1) == ' ' || l.charAt(1) == '\t')) {
                int eq = arg.indexOf('=');
                if (eq < 0) continue;
                String var = arg.substring(0, eq).trim().toUpperCase(Locale.ROOT), val = arg.substring(eq + 1).trim();
                if (var.equals("X")) {
                    double[] c = parseCoordPair(val);
                    if (c != null) {
                        cx = c[0];
                        cy = c[1];
                    }
                } else if (var.equals("D")) {
                    cw = !val.contains("-");
                }
                continue;
            }
            if (k0 != 'D') continue;
            try {
                if (k1 == 'P') {
                    double[] c = parseCoordPair(arg);
                    if (c != null) add(z, c[0], c[1]);
                } else if (k1 == 'C') {
                    arc(z, cx, cy, Double.parseDouble(arg.split("\\s+")[0]) * NM, 0, 360, true);
                } else if (k1 == 'A') {
                    String[] p = arg.replace(',', ' ').trim().split("\\s+");
                    if (p.length >= 3)
                        arc(z, cx, cy, Double.parseDouble(p[0]) * NM, Double.parseDouble(p[1]), Double.parseDouble(p[2]), cw);
                } else if (k1 == 'B') {
                    int comma = arg.indexOf(',');
                    if (comma < 0) continue;
                    double[] a = parseCoordPair(arg.substring(0, comma)), b = parseCoordPair(arg.substring(comma + 1));
                    if (a == null || b == null) continue;
                    double r = 0.5 * (Geo.distance(cx, cy, a[0], a[1]) + Geo.distance(cx, cy, b[0], b[1]));
                    arc(z, cx, cy, r, Geo.bearing(cx, cy, a[0], a[1]), Geo.bearing(cx, cy, b[0], b[1]), cw);
                }
            } catch (NumberFormatException ignored) {
                // malformed line: skip it, like the firmware does
            }
        }
        finish(z, out);
        return out;
    }
}
