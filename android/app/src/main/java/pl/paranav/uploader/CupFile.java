package pl.paranav.uploader;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** SeeYou .cup waypoint files: read and write (pure Java, tested on the desktop). */
final class CupFile {
    private CupFile() {}

    static final class Wpt {
        String name = "", code = "";
        double lat, lon;
        Float elev;    // metres AMSL, null = unknown (ParaNav takes it from its terrain data)
        int style = 1; // 1 waypoint, 2..5 landing fields

        Wpt() {}

        Wpt(String name, double lat, double lon) {
            this.name = name;
            this.lat = lat;
            this.lon = lon;
        }

        Wpt copy() {
            Wpt w = new Wpt(name, lat, lon);
            w.code = code;
            w.elev = elev;
            w.style = style;
            return w;
        }

        boolean isLanding() {
            return style >= 2 && style <= 5;
        }
    }

    static final String HEADER = "name,code,country,lat,lon,elev,style,rwdir,rwlen,freq,desc";

    static List<String> splitCsv(String line) {
        List<String> out = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        boolean q = false;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (c == '"') {
                if (q && i + 1 < line.length() && line.charAt(i + 1) == '"') {
                    cur.append('"');
                    i++;
                } else {
                    q = !q;
                }
            } else if (c == ',' && !q) {
                out.add(cur.toString());
                cur.setLength(0);
            } else if (c != '\r' && c != '\n') {
                cur.append(c);
            }
        }
        out.add(cur.toString());
        return out;
    }

    /** "5212.345N" / "02101.234E" -> degrees, or NaN. */
    static double parseCoord(String s, boolean lon) {
        s = s.trim().toUpperCase(Locale.ROOT);
        if (s.length() < 6) return Double.NaN;
        char h = s.charAt(s.length() - 1);
        int dd = lon ? 3 : 2;
        try {
            double deg = Double.parseDouble(s.substring(0, dd));
            double min = Double.parseDouble(s.substring(dd, s.length() - 1));
            double v = deg + min / 60.0;
            if (h == 'S' || h == 'W') v = -v;
            if (lon ? (h != 'E' && h != 'W') : (h != 'N' && h != 'S')) return Double.NaN;
            return v;
        } catch (NumberFormatException e) {
            return Double.NaN;
        }
    }

    static String formatCoord(double v, boolean lon) {
        char h = lon ? (v < 0 ? 'W' : 'E') : (v < 0 ? 'S' : 'N');
        v = Math.abs(v);
        long thousandths = Math.round(v * 60000.0);  // minutes * 1000
        long deg = thousandths / 60000, rest = thousandths % 60000;
        return String.format(Locale.ROOT, lon ? "%03d%02d.%03d%c" : "%02d%02d.%03d%c", deg, rest / 1000, rest % 1000, h);
    }

    static List<Wpt> parse(String text) {
        List<Wpt> out = new ArrayList<>();
        if (text.startsWith("﻿")) text = text.substring(1);
        for (String line : text.split("\\r?\\n")) {
            if (line.startsWith("-----Related Tasks")) break;
            if (line.trim().isEmpty() || line.toLowerCase(Locale.ROOT).startsWith("name,")) continue;
            List<String> f = splitCsv(line);
            if (f.size() < 6) continue;
            double la = parseCoord(f.get(3), false), lo = parseCoord(f.get(4), true);
            if (Double.isNaN(la) || Double.isNaN(lo)) continue;
            Wpt w = new Wpt(f.get(0), la, lo);
            w.code = f.get(1);
            String e = f.get(5).trim().toLowerCase(Locale.ROOT);
            if (e.matches(".*\\d.*")) {
                try {
                    float v = Float.parseFloat(e.replaceAll("[^0-9.\\-]", ""));
                    w.elev = e.contains("ft") ? v * 0.3048f : v;
                } catch (NumberFormatException ignored) {
                    w.elev = null;
                }
            }
            if (f.size() > 6) {
                try {
                    w.style = Integer.parseInt(f.get(6).trim());
                } catch (NumberFormatException ignored) {
                    w.style = 1;
                }
            }
            out.add(w);
        }
        return out;
    }

    private static String quote(String s) {
        return "\"" + s.replace("\"", "\"\"").replace('\n', ' ') + "\"";
    }

    /** Short code from the name: letters and digits, upper case, max 6 characters. */
    static String codeFor(String name) {
        String c = name.toUpperCase(Locale.ROOT).replaceAll("[^A-Z0-9]", "");
        return c.length() > 6 ? c.substring(0, 6) : c;
    }

    static String write(List<Wpt> pts) {
        StringBuilder sb = new StringBuilder(HEADER).append("\r\n");
        for (Wpt w : pts) {
            String code = w.code == null || w.code.isEmpty() ? codeFor(w.name) : w.code;
            sb.append(quote(w.name)).append(',').append(quote(code)).append(",,")
                    .append(formatCoord(w.lat, false)).append(',').append(formatCoord(w.lon, true)).append(',')
                    .append(w.elev == null ? "" : String.format(Locale.ROOT, "%.1fm", w.elev)).append(',')
                    .append(w.style).append(",,,,\r\n");
        }
        return sb.toString();
    }

    /** Total length of the legs [m]. */
    static double length(List<Wpt> pts) {
        double d = 0;
        for (int i = 1; i < pts.size(); i++)
            d += Geo.distance(pts.get(i - 1).lat, pts.get(i - 1).lon, pts.get(i).lat, pts.get(i).lon);
        return d;
    }
}
