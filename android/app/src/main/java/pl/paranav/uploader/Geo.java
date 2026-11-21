package pl.paranav.uploader;

/** Geodesy and Web Mercator helpers (pure Java, tested on the desktop). */
final class Geo {
    private Geo() {}

    static final double EARTH_R = 6371008.8;

    /** lat/lon -> normalised Web Mercator world coordinates 0..1 (x east, y south). */
    static double worldX(double lon) {
        return (lon + 180.0) / 360.0;
    }

    static double worldY(double lat) {
        lat = Math.max(-85.0511, Math.min(85.0511, lat));
        double s = Math.sin(Math.toRadians(lat));
        return 0.5 - Math.log((1 + s) / (1 - s)) / (4 * Math.PI);
    }

    static double lon(double wx) {
        return wx * 360.0 - 180.0;
    }

    static double lat(double wy) {
        return Math.toDegrees(Math.atan(Math.sinh(Math.PI * (1 - 2 * wy))));
    }

    /** Great-circle distance [m]. */
    static double distance(double lat1, double lon1, double lat2, double lon2) {
        double p1 = Math.toRadians(lat1), p2 = Math.toRadians(lat2);
        double dp = p2 - p1, dl = Math.toRadians(lon2 - lon1);
        double a = Math.sin(dp / 2) * Math.sin(dp / 2) + Math.cos(p1) * Math.cos(p2) * Math.sin(dl / 2) * Math.sin(dl / 2);
        return 2 * EARTH_R * Math.asin(Math.min(1, Math.sqrt(a)));
    }

    /** Initial bearing [deg 0..360). */
    static double bearing(double lat1, double lon1, double lat2, double lon2) {
        double p1 = Math.toRadians(lat1), p2 = Math.toRadians(lat2), dl = Math.toRadians(lon2 - lon1);
        double y = Math.sin(dl) * Math.cos(p2);
        double x = Math.cos(p1) * Math.sin(p2) - Math.sin(p1) * Math.cos(p2) * Math.cos(dl);
        double b = Math.toDegrees(Math.atan2(y, x));
        return (b + 360) % 360;
    }

    /** Point at distance [m] and bearing [deg] from (lat, lon): returns {lat, lon}. */
    static double[] destination(double lat, double lon, double brg, double dist) {
        double d = dist / EARTH_R, b = Math.toRadians(brg), p1 = Math.toRadians(lat), l1 = Math.toRadians(lon);
        double p2 = Math.asin(Math.sin(p1) * Math.cos(d) + Math.cos(p1) * Math.sin(d) * Math.cos(b));
        double l2 = l1 + Math.atan2(Math.sin(b) * Math.sin(d) * Math.cos(p1), Math.cos(d) - Math.sin(p1) * Math.sin(p2));
        return new double[] {Math.toDegrees(p2), ((Math.toDegrees(l2) + 540) % 360) - 180};
    }
}
