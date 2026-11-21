package pl.paranav.uploader;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Desktop test of the route planner helpers (Geo, CupFile, OpenAir) - no Android needed. */
public class PlannerTest {
    static int fails = 0, checks = 0;

    static void check(boolean c, String what) {
        checks++;
        if (!c) {
            fails++;
            System.out.println("FAIL " + what);
        }
    }

    public static void main(String[] a) throws Exception {
        String sd = a.length > 0 ? a[0] : "sd_card";
        // coordinates
        check(CupFile.formatCoord(52.5, false).equals("5230.000N"), "lat 52.5");
        check(CupFile.formatCoord(21.0 + 2.0 / 60, true).equals("02102.000E"), "lon 21 02");
        check(CupFile.formatCoord(52.99999999, false).equals("5300.000N"), "minute carry");
        check(CupFile.formatCoord(-33.5, false).equals("3330.000S"), "south");
        check(CupFile.formatCoord(-70.25, true).equals("07015.000W"), "west");
        check(Math.abs(CupFile.parseCoord("5212.345N", false) - (52 + 12.345 / 60)) < 1e-9, "parse lat");
        check(Double.isNaN(CupFile.parseCoord("5212.345X", false)), "bad hemisphere");
        // round trip, unknown elevation, landing style, quotes in names
        List<CupFile.Wpt> pts = new ArrayList<>();
        pts.add(new CupFile.Wpt("Łąka \"Duża\", start", 54.35, 18.65));
        CupFile.Wpt l = new CupFile.Wpt("Lądowisko Rumia", 54.57, 18.39);
        l.style = 3;
        l.elev = 42f;
        pts.add(l);
        pts.add(new CupFile.Wpt("Hel", 54.6, 18.8));
        String cup = CupFile.write(pts);
        List<CupFile.Wpt> back = CupFile.parse(cup);
        check(back.size() == 3, "round trip count");
        check(back.get(0).name.equals("Łąka \"Duża\", start") && back.get(0).elev == null, "name with quotes, no elev");
        check(back.get(1).isLanding() && Math.abs(back.get(1).elev - 42f) < 0.01, "landing + elev");
        for (int i = 0; i < 3; i++)
            check(Math.abs(back.get(i).lat - pts.get(i).lat) < 1e-5 && Math.abs(back.get(i).lon - pts.get(i).lon) < 1e-5,
                    "round trip position " + i);
        Files.write(Paths.get("/tmp/planner_route.cup"), cup.getBytes(StandardCharsets.UTF_8));
        System.out.printf("java length %.1f%n", CupFile.length(pts));
        // geo
        check(Math.abs(Geo.distance(54, 18, 54 + 1.0 / 60, 18) - 1853.2) < 2, "1 arc minute");
        check(Math.abs(Geo.bearing(54, 18, 54, 18.1) - 90) < 0.1, "bearing east");
        double[] d = Geo.destination(54, 18, 45, 10000);
        check(Math.abs(Geo.distance(54, 18, d[0], d[1]) - 10000) < 1, "destination");
        check(Math.abs(Geo.lat(Geo.worldY(54.35)) - 54.35) < 1e-9 && Math.abs(Geo.lon(Geo.worldX(18.65)) - 18.65) < 1e-9, "mercator");
        // example files
        String w = new String(Files.readAllBytes(Paths.get(sd, "waypoints", "EXAMPLE_waypoints.cup")), StandardCharsets.UTF_8);
        check(CupFile.parse(w).size() == 4, "example waypoints");
        String air = new String(Files.readAllBytes(Paths.get(sd, "airspace", "EXAMPLE_airspace.txt")), StandardCharsets.UTF_8);
        List<OpenAir.Zone> zs = OpenAir.parse(air);
        check(zs.size() == 4, "example airspace zones: " + zs.size());
        OpenAir.Zone ctr = zs.get(0);
        check(ctr.name.equals("CTR TEST") && ctr.category() == 1, "CTR");
        double[] c = OpenAir.parseCoordPair("52:12:00 N 020:58:00 E");
        for (double[] p : ctr.pts) check(Math.abs(Geo.distance(c[0], c[1], p[0], p[1]) - 4 * 1852) < 5, "circle radius");
        check(zs.get(2).name.startsWith("EPTR99") && zs.get(2).category() == 0 && zs.get(2).pts.size() > 10, "class R area with arc");
        check(zs.get(3).category() == 0, "EPP prohibited");
        check(OpenAir.parseCoordPair("522430N 0205800E") != null, "compact DMS");
        // route vs airspace (same square as test/host_tests.cpp testEscape)
        List<OpenAir.Zone> rcz = OpenAir.parse(
                "AC R\nAN SQUARE\nAL 300m MSL\nAH 1000m MSL\n"
                + "DP 52:00:00 N 021:00:00 E\nDP 52:06:00 N 021:00:00 E\nDP 52:06:00 N 021:09:36 E\nDP 52:00:00 N 021:09:36 E\n"
                + "AC P\nAN HIGH\nAL FL65\nAH FL95\n"
                + "DP 52:00:00 N 021:04:48 E\nDP 52:06:00 N 021:04:48 E\nDP 52:06:00 N 021:09:36 E\nDP 52:00:00 N 021:09:36 E\n"
                + "AC G\nAN IGNORED\nAL GND\nAH 5000m MSL\n"
                + "DP 51:00:00 N 020:00:00 E\nDP 53:00:00 N 020:00:00 E\nDP 53:00:00 N 022:00:00 E\nDP 51:00:00 N 022:00:00 E\n");
        check(rcz.size() == 3, "route check zones " + rcz.size());
        List<CupFile.Wpt> rp = new ArrayList<>();
        rp.add(new CupFile.Wpt("A", 52.05, 20.9));
        rp.add(new CupFile.Wpt("B", 52.05, 21.3));
        rp.add(new CupFile.Wpt("C", 52.2, 21.3));
        List<RouteCheck.Hit> hits = RouteCheck.check(rcz, rp);
        check(hits.size() == 2, "route check hits " + hits.size());
        if (hits.size() == 2) {
            check(hits.get(0).zone.name.equals("SQUARE") && hits.get(0).conflict, "square conflict");
            check(Math.abs(hits.get(0).km - 6.9) < 0.3, "square km " + hits.get(0).km);
            check(hits.get(1).zone.name.equals("HIGH") && !hits.get(1).conflict, "FL65 floor not a conflict");
        }
        check(Math.abs(RouteCheck.floorMetres("1500ft AMSL") - 457.2) < 0.1, "1500ft");
        check(RouteCheck.floorMetres("GND") == 0 && RouteCheck.floorMetres("SFC") == 0, "GND");
        check(Math.abs(RouteCheck.floorMetres("762m") - 762) < 0.1, "762m");
        check(Math.abs(RouteCheck.floorMetres("FL 65") - 1981.2) < 0.1, "FL 65");
        Tr.pl = true;
        check(RouteCheck.summary(hits).startsWith("Trasa przecina 1 stref"), "summary");
        rp.remove(1);
        rp.set(1, new CupFile.Wpt("C", 52.2, 20.9));
        check(RouteCheck.check(rcz, rp).isEmpty() && RouteCheck.summary(RouteCheck.check(rcz, rp)).isEmpty(), "clear route");
        System.out.println(checks + " checks, " + fails + " failures");
        System.exit(fails == 0 ? 0 : 1);
    }
}
