package pl.paranav.uploader;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.LruCache;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.Set;

/**
 * OpenTopoMap tiles: memory cache + disk cache (app cache folder) + polite download (2 threads,
 * newest request first, identifying User-Agent). Map data (c) OpenStreetMap contributors, SRTM;
 * map style (c) OpenTopoMap (CC-BY-SA). Tiles are only downloaded for what is shown on the screen
 * (no bulk download - not allowed by the tile server rules).
 */
final class TileCache {
    static final int MAX_ZOOM = 17, MIN_ZOOM = 3;
    static final String ATTRIBUTION = "© OpenStreetMap contributors, SRTM | © OpenTopoMap (CC-BY-SA)";
    private static final String UA = "ParaNav-Android/1.0 (paramotor route planner)";
    private static final long MAX_AGE_MS = 30L * 24 * 3600 * 1000;  // re-download after 30 days (when online)

    interface Listener {
        void onTileLoaded();
    }

    private final File dir;
    private final LruCache<String, Bitmap> mem;
    private final LinkedList<long[]> queue = new LinkedList<>();  // {z, x, y}, newest first
    private final Set<String> pending = new HashSet<>();
    private final Listener listener;
    private volatile boolean offline;  // last download failed (shown on the map)
    private volatile int failures;
    private final Thread[] threads = new Thread[2];
    private volatile boolean stop;

    TileCache(Context c, Listener l) {
        dir = new File(c.getCacheDir(), "tiles/opentopomap");
        listener = l;
        int kb = (int) (Runtime.getRuntime().maxMemory() / 1024 / 6);
        mem = new LruCache<String, Bitmap>(kb) {
            @Override
            protected int sizeOf(String k, Bitmap b) {
                return b.getByteCount() / 1024;
            }
        };
        for (int i = 0; i < threads.length; i++) {
            threads[i] = new Thread(this::run, "tiles-" + i);
            threads[i].setDaemon(true);
            threads[i].start();
        }
    }

    boolean isOffline() {
        return offline;
    }

    void shutdown() {
        stop = true;
        synchronized (queue) {
            queue.notifyAll();
        }
    }

    private static String key(int z, int x, int y) {
        return z + "/" + x + "/" + y;
    }

    private File file(int z, int x, int y) {
        return new File(dir, z + "/" + x + "/" + y + ".png");
    }

    /** Tile from memory, or null (then it is loaded from disk / downloaded in the background). */
    Bitmap get(int z, int x, int y) {
        String k = key(z, x, y);
        Bitmap b = mem.get(k);
        if (b != null) return b;
        synchronized (queue) {
            if (pending.add(k)) {
                queue.addFirst(new long[] {z, x, y});
                while (queue.size() > 120) {  // the view moved on: forget old requests
                    long[] o = queue.removeLast();
                    pending.remove(key((int) o[0], (int) o[1], (int) o[2]));
                }
                queue.notify();
            }
        }
        return null;
    }

    /** Only from memory (for the "use a lower zoom tile while loading" fallback). */
    Bitmap peek(int z, int x, int y) {
        return mem.get(key(z, x, y));
    }

    private void run() {
        while (!stop) {
            long[] t;
            synchronized (queue) {
                while (queue.isEmpty() && !stop) {
                    try {
                        queue.wait();
                    } catch (InterruptedException e) {
                        return;
                    }
                }
                if (stop) return;
                t = queue.removeFirst();
            }
            int z = (int) t[0], x = (int) t[1], y = (int) t[2];
            Bitmap b = load(z, x, y);
            synchronized (queue) {
                pending.remove(key(z, x, y));
            }
            if (b != null) {
                mem.put(key(z, x, y), b);
                listener.onTileLoaded();
            }
        }
    }

    private Bitmap load(int z, int x, int y) {
        File f = file(z, x, y);
        Bitmap disk = f.exists() ? BitmapFactory.decodeFile(f.getPath()) : null;
        boolean fresh = disk != null && System.currentTimeMillis() - f.lastModified() < MAX_AGE_MS;
        if (fresh) return disk;
        // after repeated failures try the network only now and then (offline: use what is on disk)
        if (failures > 3 && disk == null && (System.nanoTime() / 1_000_000_000L) % 10 != 0) return null;
        byte[] data = download(z, x, y);
        if (data == null) return disk;
        Bitmap b = BitmapFactory.decodeByteArray(data, 0, data.length);
        if (b == null) return disk;
        try {
            f.getParentFile().mkdirs();
            Store.write(f, data);
        } catch (IOException ignored) {
            // cache full or not writable: still show the tile
        }
        return b;
    }

    private byte[] download(int z, int x, int y) {
        char sub = "abc".charAt((x + y) % 3);
        HttpURLConnection c = null;
        try {
            URL u = new URL("https://" + sub + ".tile.opentopomap.org/" + z + "/" + x + "/" + y + ".png");
            c = (HttpURLConnection) u.openConnection();
            c.setRequestProperty("User-Agent", UA);
            c.setConnectTimeout(8000);
            c.setReadTimeout(15000);
            if (c.getResponseCode() != 200) throw new IOException("HTTP " + c.getResponseCode());
            try (InputStream in = c.getInputStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                offline = false;
                failures = 0;
                return out.toByteArray();
            }
        } catch (IOException e) {
            offline = true;
            failures++;
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }
}
