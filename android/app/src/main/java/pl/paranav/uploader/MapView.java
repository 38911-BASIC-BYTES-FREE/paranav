package pl.paranav.uploader;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Slippy map without external libraries: OpenTopoMap tiles + airspace, waypoint and route
 * overlays. Drag = pan, pinch / double tap = zoom, tap = add point, long press on a route point
 * and drag = move it, tap on a route point = edit it.
 */
public class MapView extends View implements TileCache.Listener {
    interface Listener {
        /** Tap on the map. routeIdx >= 0: a route point was tapped; wpt != null: an overlay waypoint. */
        void onMapTap(double lat, double lon, int routeIdx, CupFile.Wpt wpt);

        void onPointMoved(int idx);

        void onPointDragStart(int idx);
    }

    private final TileCache tiles;
    private final float density;
    private final float tileSize;  // screen px of one 256 px tile at an integer zoom
    private double cx = Geo.worldX(19.4), cy = Geo.worldY(52.0), zoom = 6;  // view centre (world 0..1) and zoom
    private List<CupFile.Wpt> route = new ArrayList<>();
    private List<OpenAir.Zone> airspace = new ArrayList<>();
    private List<CupFile.Wpt> waypoints = new ArrayList<>();
    private boolean showAirspace = true, showWaypoints = true;
    private Listener listener;
    private int dragging = -1;

    private final Paint bmpPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG), halo = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint box = new Paint();
    private final GestureDetector gestures;
    private final ScaleGestureDetector scaler;

    private static final int[] ZONE_COLORS = {0xFFC62828, 0xFFAD1457, 0xFFEF6C00, 0xFF1565C0};
    private static final int ROUTE_COLOR = 0xFF6A1B9A, WPT_COLOR = 0xFF2E7D32;

    public MapView(Context c) {
        super(c);
        density = c.getResources().getDisplayMetrics().density;
        tileSize = 256f * Math.max(1f, density / 1.6f);  // tiles slightly enlarged on dense screens (readable labels)
        tiles = new TileCache(c, this);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeJoin(Paint.Join.ROUND);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        text.setTextSize(12 * density);
        text.setColor(Color.BLACK);
        halo.setTextSize(12 * density);
        halo.setColor(Color.WHITE);
        halo.setStyle(Paint.Style.STROKE);
        halo.setStrokeWidth(3 * density);
        box.setColor(0xCCFFFFFF);
        gestures = new GestureDetector(c, new GestureDetector.SimpleOnGestureListener() {
            @Override
            public boolean onDown(MotionEvent e) {
                return true;
            }

            @Override
            public boolean onScroll(MotionEvent e1, MotionEvent e2, float dx, float dy) {
                if (dragging >= 0) return false;
                double ppw = ppw();
                cx = ((cx + dx / ppw) % 1 + 1) % 1;
                cy = Math.max(0, Math.min(1, cy + dy / ppw));
                invalidate();
                return true;
            }

            @Override
            public boolean onDoubleTap(MotionEvent e) {
                zoomAround(1, e.getX(), e.getY());
                return true;
            }

            @Override
            public boolean onSingleTapConfirmed(MotionEvent e) {
                if (listener == null) return false;
                int ri = hitRoute(e.getX(), e.getY());
                CupFile.Wpt w = ri < 0 ? hitWaypoint(e.getX(), e.getY()) : null;
                listener.onMapTap(lat(e.getY()), lon(e.getX()), ri, w);
                return true;
            }

            @Override
            public void onLongPress(MotionEvent e) {
                int ri = hitRoute(e.getX(), e.getY());
                if (ri >= 0) {
                    dragging = ri;
                    if (listener != null) listener.onPointDragStart(ri);
                    performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS);
                    invalidate();
                }
            }
        });
        gestures.setIsLongpressEnabled(true);
        scaler = new ScaleGestureDetector(c, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override
            public boolean onScale(ScaleGestureDetector d) {
                zoomAround(Math.log(d.getScaleFactor()) / Math.log(2), d.getFocusX(), d.getFocusY());
                return true;
            }
        });
    }

    // ------------------------------------------------------------------ public API
    void setListener(Listener l) {
        listener = l;
    }

    void setRoute(List<CupFile.Wpt> r) {
        route = r;
        invalidate();
    }

    void setOverlays(List<OpenAir.Zone> zones, List<CupFile.Wpt> wpts) {
        airspace = zones;
        waypoints = wpts;
        invalidate();
    }

    void setShowAirspace(boolean b) {
        showAirspace = b;
        invalidate();
    }

    void setShowWaypoints(boolean b) {
        showWaypoints = b;
        invalidate();
    }

    boolean showAirspace() {
        return showAirspace;
    }

    boolean showWaypoints() {
        return showWaypoints;
    }

    double centerLat() {
        return Geo.lat(cy);
    }

    double centerLon() {
        return Geo.lon(cx);
    }

    double zoom() {
        return zoom;
    }

    void setView(double lat, double lon, double z) {
        cx = Geo.worldX(lon);
        cy = Geo.worldY(lat);
        zoom = clampZoom(z);
        invalidate();
    }

    void zoomBy(double dz) {
        zoomAround(dz, getWidth() / 2f, getHeight() / 2f);
    }

    /** Zoom so that the whole route is visible. */
    void fitRoute() {
        if (route.isEmpty() || getWidth() == 0) return;
        double x0 = 1, x1 = 0, y0 = 1, y1 = 0;
        for (CupFile.Wpt w : route) {
            double x = Geo.worldX(w.lon), y = Geo.worldY(w.lat);
            x0 = Math.min(x0, x);
            x1 = Math.max(x1, x);
            y0 = Math.min(y0, y);
            y1 = Math.max(y1, y);
        }
        cx = (x0 + x1) / 2;
        cy = (y0 + y1) / 2;
        double span = Math.max(Math.max(x1 - x0, (y1 - y0) * getWidth() / Math.max(1, getHeight())), 1e-6);
        zoom = clampZoom(Math.log(getWidth() * 0.8 / (span * tileSize)) / Math.log(2));
        if (route.size() == 1) zoom = 13;
        invalidate();
    }

    void shutdown() {
        tiles.shutdown();
    }

    @Override
    public void onTileLoaded() {
        postInvalidate();
    }

    // ------------------------------------------------------------------ projection
    private double ppw() {  // screen pixels per world unit
        return tileSize * Math.pow(2, zoom);
    }

    private double clampZoom(double z) {
        return Math.max(TileCache.MIN_ZOOM, Math.min(TileCache.MAX_ZOOM + 1, z));
    }

    float sx(double lon) {
        double d = Geo.worldX(lon) - cx;
        if (d > 0.5) d -= 1;
        if (d < -0.5) d += 1;
        return (float) (d * ppw() + getWidth() / 2.0);
    }

    float sy(double lat) {
        return (float) ((Geo.worldY(lat) - cy) * ppw() + getHeight() / 2.0);
    }

    double lon(float x) {
        double wx = cx + (x - getWidth() / 2.0) / ppw();
        return Geo.lon(((wx % 1) + 1) % 1);
    }

    double lat(float y) {
        return Geo.lat(cy + (y - getHeight() / 2.0) / ppw());
    }

    private void zoomAround(double dz, float fx, float fy) {
        double ppw0 = ppw();
        double wx = cx + (fx - getWidth() / 2.0) / ppw0, wy = cy + (fy - getHeight() / 2.0) / ppw0;
        zoom = clampZoom(zoom + dz);
        double ppw1 = ppw();
        cx = ((wx - (fx - getWidth() / 2.0) / ppw1) % 1 + 1) % 1;
        cy = Math.max(0, Math.min(1, wy - (fy - getHeight() / 2.0) / ppw1));
        invalidate();
    }

    private int hitRoute(float x, float y) {
        float r = 26 * density;
        for (int i = route.size() - 1; i >= 0; i--) {
            CupFile.Wpt w = route.get(i);
            if (Math.hypot(sx(w.lon) - x, sy(w.lat) - y) < r) return i;
        }
        return -1;
    }

    private CupFile.Wpt hitWaypoint(float x, float y) {
        if (!showWaypoints || zoom < 8) return null;
        float r = 20 * density;
        CupFile.Wpt best = null;
        double bd = r;
        for (CupFile.Wpt w : waypoints) {
            double d = Math.hypot(sx(w.lon) - x, sy(w.lat) - y);
            if (d < bd) {
                bd = d;
                best = w;
            }
        }
        return best;
    }

    // ------------------------------------------------------------------ touch
    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (dragging >= 0) {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_MOVE && dragging < route.size()) {
                CupFile.Wpt w = route.get(dragging);
                w.lat = lat(e.getY());
                w.lon = lon(e.getX());
                w.elev = null;  // moved: elevation unknown again
                invalidate();
            } else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) {
                int i = dragging;
                dragging = -1;
                if (listener != null) listener.onPointMoved(i);
                invalidate();
            }
            return true;
        }
        scaler.onTouchEvent(e);
        if (!scaler.isInProgress()) gestures.onTouchEvent(e);
        return true;
    }

    // ------------------------------------------------------------------ drawing
    @Override
    protected void onDraw(Canvas c) {
        c.drawColor(0xFFEFEDE6);
        drawTiles(c);
        if (showAirspace) drawAirspace(c);
        if (showWaypoints) drawWaypoints(c);
        drawRoute(c);
        drawDecorations(c);
    }

    private void drawTiles(Canvas c) {
        int tz = (int) Math.max(TileCache.MIN_ZOOM, Math.min(TileCache.MAX_ZOOM, Math.round(zoom)));
        int n = 1 << tz;
        double ppw = ppw(), tsz = ppw / n;
        double left = cx - getWidth() / 2.0 / ppw, top = cy - getHeight() / 2.0 / ppw;
        int tx0 = (int) Math.floor(left * n), ty0 = (int) Math.floor(top * n);
        int tx1 = (int) Math.floor((left + getWidth() / ppw) * n), ty1 = (int) Math.floor((top + getHeight() / ppw) * n);
        Rect src = new Rect();
        RectF dst = new RectF();
        for (int ty = Math.max(0, ty0); ty <= Math.min(n - 1, ty1); ty++)
            for (int tx = tx0; tx <= tx1; tx++) {
                int wx = ((tx % n) + n) % n;
                float x = (float) ((tx / (double) n - left) * ppw), y = (float) ((ty / (double) n - top) * ppw);
                dst.set(x, y, (float) (x + tsz) + 0.5f, (float) (y + tsz) + 0.5f);
                Bitmap b = tiles.get(tz, wx, ty);
                if (b != null) {
                    c.drawBitmap(b, null, dst, bmpPaint);
                    continue;
                }
                // while loading: enlarge a part of a lower-zoom tile that is already in memory
                for (int k = 1; k <= 4 && tz - k >= 0; k++) {
                    Bitmap p = tiles.peek(tz - k, wx >> k, ty >> k);
                    if (p == null) continue;
                    int part = 256 >> k, ox = (wx - ((wx >> k) << k)) * part, oy = (ty - ((ty >> k) << k)) * part;
                    src.set(ox, oy, ox + part, oy + part);
                    c.drawBitmap(p, src, dst, bmpPaint);
                    break;
                }
            }
    }

    private boolean inView(double minLat, double maxLat, double minLon, double maxLon) {
        double la0 = lat(getHeight()), la1 = lat(0), lo0 = lon(0), lo1 = lon(getWidth());
        if (maxLat < la0 || minLat > la1) return false;
        if (lo0 <= lo1) return !(maxLon < lo0 || minLon > lo1);
        return true;  // view crosses the date line
    }

    private void label(Canvas c, String s, float x, float y, Paint.Align a) {
        text.setTextAlign(a);
        halo.setTextAlign(a);
        c.drawText(s, x, y, halo);
        c.drawText(s, x, y, text);
    }

    private void drawAirspace(Canvas c) {
        Path p = new Path();
        stroke.setStrokeWidth(2 * density);
        for (OpenAir.Zone z : airspace) {
            if (!inView(z.minLat, z.maxLat, z.minLon, z.maxLon)) continue;
            p.reset();
            boolean first = true;
            for (double[] q : z.pts) {
                float x = sx(q[1]), y = sy(q[0]);
                if (first) p.moveTo(x, y);
                else p.lineTo(x, y);
                first = false;
            }
            p.close();
            int col = ZONE_COLORS[z.category()];
            fill.setColor((col & 0x00FFFFFF) | 0x22000000);
            c.drawPath(p, fill);
            stroke.setColor(col);
            c.drawPath(p, stroke);
            float w = sx(z.maxLon) - sx(z.minLon);
            if (zoom >= 8 && w > 140 * density) {
                float x = (sx(z.minLon) + sx(z.maxLon)) / 2, y = (sy(z.minLat) + sy(z.maxLat)) / 2;
                text.setColor(col);
                label(c, z.name, x, y, Paint.Align.CENTER);
                label(c, z.lower + " – " + z.upper, x, y + 14 * density, Paint.Align.CENTER);
                text.setColor(Color.BLACK);
            }
        }
    }

    private void drawWaypoints(Canvas c) {
        if (zoom < 8) return;
        fill.setColor(WPT_COLOR);
        float r = 4 * density;
        for (CupFile.Wpt w : waypoints) {
            float x = sx(w.lon), y = sy(w.lat);
            if (x < -50 || y < -50 || x > getWidth() + 50 || y > getHeight() + 50) continue;
            if (w.isLanding()) c.drawRect(x - r, y - r, x + r, y + r, fill);
            else c.drawCircle(x, y, r, fill);
            if (zoom >= 10) {
                text.setColor(WPT_COLOR);
                label(c, w.name, x + 7 * density, y + 4 * density, Paint.Align.LEFT);
                text.setColor(Color.BLACK);
            }
        }
    }

    private void drawRoute(Canvas c) {
        if (route.isEmpty()) return;
        stroke.setColor(ROUTE_COLOR);
        stroke.setStrokeWidth(4 * density);
        Path p = new Path();
        for (int i = 0; i < route.size(); i++) {
            float x = sx(route.get(i).lon), y = sy(route.get(i).lat);
            if (i == 0) p.moveTo(x, y);
            else p.lineTo(x, y);
        }
        c.drawPath(p, stroke);
        // leg lengths in the middle of each leg
        if (zoom >= 9)
            for (int i = 1; i < route.size(); i++) {
                CupFile.Wpt a = route.get(i - 1), b = route.get(i);
                float x = (sx(a.lon) + sx(b.lon)) / 2, y = (sy(a.lat) + sy(b.lat)) / 2;
                if (Math.hypot(sx(a.lon) - sx(b.lon), sy(a.lat) - sy(b.lat)) < 90 * density) continue;
                double km = Geo.distance(a.lat, a.lon, b.lat, b.lon) / 1000;
                text.setColor(ROUTE_COLOR);
                label(c, String.format(Locale.ROOT, km < 10 ? "%.1f km" : "%.0f km", km), x, y - 6 * density,
                        Paint.Align.CENTER);
                text.setColor(Color.BLACK);
            }
        float r = 12 * density;
        stroke.setStrokeWidth(3 * density);
        for (int i = 0; i < route.size(); i++) {
            CupFile.Wpt w = route.get(i);
            float x = sx(w.lon), y = sy(w.lat);
            fill.setColor(i == dragging ? 0xFFFFF59D : Color.WHITE);
            if (w.isLanding()) {
                c.drawRect(x - r, y - r, x + r, y + r, fill);
                c.drawRect(x - r, y - r, x + r, y + r, stroke);
            } else {
                c.drawCircle(x, y, r, fill);
                c.drawCircle(x, y, r, stroke);
            }
            text.setColor(ROUTE_COLOR);
            text.setTextAlign(Paint.Align.CENTER);
            text.setFakeBoldText(true);
            c.drawText(String.valueOf(i + 1), x, y + 4 * density, text);
            text.setFakeBoldText(false);
            if (zoom >= 10) label(c, w.name, x + r + 4 * density, y - r, Paint.Align.LEFT);
            text.setColor(Color.BLACK);
        }
    }

    private void drawDecorations(Canvas c) {
        float pad = 4 * density, small = 10 * density;
        text.setTextSize(small);
        // attribution (required by the map licence)
        float tw = text.measureText(TileCache.ATTRIBUTION);
        c.drawRect(0, getHeight() - small - 2 * pad, tw + 2 * pad, getHeight(), box);
        text.setTextAlign(Paint.Align.LEFT);
        c.drawText(TileCache.ATTRIBUTION, pad, getHeight() - pad - 2, text);
        // scale bar (bottom right)
        double mpp = 40075016.686 * Math.cos(Math.toRadians(centerLat())) / ppw();
        double target = 100 * density * mpp, m = 1;
        for (double s : new double[] {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000})
            if (s <= target) m = s;
        float len = (float) (m / mpp), x1 = getWidth() - 2 * pad, y = getHeight() - small - 4 * pad;
        String s = m >= 1000 ? String.format(Locale.ROOT, "%.0f km", m / 1000) : String.format(Locale.ROOT, "%.0f m", m);
        c.drawRect(x1 - len - pad, y - small - 2 * pad, x1 + pad, y + 2 * pad, box);
        stroke.setColor(Color.BLACK);
        stroke.setStrokeWidth(2 * density);
        c.drawLine(x1 - len, y, x1, y, stroke);
        text.setTextAlign(Paint.Align.RIGHT);
        c.drawText(s, x1, y - pad, text);
        if (tiles.isOffline()) {
            String o = Tr.t("Brak internetu – mapa tylko z pamięci telefonu", "No internet – map from the phone's cache only");
            float ow = text.measureText(o);
            c.drawRect(getWidth() / 2f - ow / 2 - pad, pad, getWidth() / 2f + ow / 2 + pad, pad * 3 + small, box);
            text.setTextAlign(Paint.Align.CENTER);
            c.drawText(o, getWidth() / 2f, pad * 2 + small, text);
        }
        text.setTextSize(12 * density);
    }
}
