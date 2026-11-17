// End-to-end test of the upload protocol: the app's Uploader.java (phone side) talks to the
// firmware's FileReceiver (compiled for the desktop as tools/xfer_sim) through a pipe.
// Packet loss is injected to exercise the resend logic. Run: tools/android_proto_test/run.sh
import pl.paranav.uploader.Link;
import pl.paranav.uploader.Uploader;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;

public class ProtoTest {
    static class PipeLink implements Link {
        final Process p;
        final BufferedWriter w;
        final LinkedBlockingQueue<String> q = new LinkedBlockingQueue<>();
        final int dropEvery;
        int dataCount = 0, dropped = 0;
        int dropAt = -1;  // drop exactly this packet number (e.g. the very last one of a file)

        PipeLink(Process p, int dropEvery) {
            this.p = p;
            this.dropEvery = dropEvery;
            w = new BufferedWriter(new OutputStreamWriter(p.getOutputStream(), StandardCharsets.UTF_8));
            Thread t = new Thread(() -> {
                try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8))) {
                    String l;
                    while ((l = r.readLine()) != null) if (l.startsWith("N ")) q.offer(l.substring(2));
                } catch (IOException ignored) {}
            });
            t.setDaemon(true);
            t.start();
        }
        public void sendCtrl(String line) throws IOException { w.write("C " + line + "\n"); w.flush(); }
        public void sendData(byte[] pkt) throws IOException {
            dataCount++;
            if ((dropEvery > 0 && dataCount % dropEvery == 0) || dataCount == dropAt) { dropped++; return; }  // lost
            StringBuilder sb = new StringBuilder("D ");
            for (byte b : pkt) sb.append(String.format("%02x", b & 0xFF));
            w.write(sb.append('\n').toString());
            w.flush();
        }
        public String waitNotify(long ms) throws InterruptedException { return q.poll(ms, TimeUnit.MILLISECONDS); }
        public void clearNotifications() { q.clear(); }
        public int maxPacket() { return 244; }
    }

    static int fails = 0;
    static void check(boolean c, String what) {
        System.out.println((c ? "  ok   " : "  FAIL ") + what);
        if (!c) fails++;
    }

    public static void main(String[] a) throws Exception {
        String sim = a[0], root = a[1], bigAirspace = a[2], sd = a[3];
        Process p = new ProcessBuilder(sim, root).redirectErrorStream(false).start();
        PipeLink link = new PipeLink(p, 37);  // drop every 37th data packet
        Uploader up = new Uploader(link);

        check(up.hello().startsWith("PARANAV 1"), "HELLO");

        byte[] air = Files.readAllBytes(Paths.get(bigAirspace));
        long t0 = System.nanoTime();
        long[] last = {0};
        String r = up.upload("airspace", "Polska strefy 2026.txt", air, true, (d, t) -> last[0] = d);
        System.out.printf("  airspace %d kB, %d packets lost and resent, %.1f s%n", air.length / 1024, link.dropped,
                (System.nanoTime() - t0) / 1e9);
        check(r.startsWith("OK END 800 airspace/Polska_strefy_2026.txt"), "airspace upload -> " + r);
        check(last[0] == air.length, "progress reached 100%");
        check(Arrays.equals(air, Files.readAllBytes(Paths.get(root, "airspace", "Polska_strefy_2026.txt"))),
                "airspace file identical on the device");

        byte[] wpt = Files.readAllBytes(Paths.get(sd, "waypoints", "PRZYKLAD_punkty.cup"));
        r = up.upload("waypoints", "punkty.cup", wpt, false, null);
        check(r.startsWith("OK END 4 waypoints/punkty.cup"), "waypoints -> " + r);

        byte[] route = Files.readAllBytes(Paths.get(sd, "route", "trasa.cup"));
        r = up.upload("route", "trasa dnia.cup", route, true, null);
        check(r.startsWith("OK END 3 route/trasa_dnia.cup"), "route -> " + r);

        byte[] act = "# z aplikacji\ndate=2026-11-16\nEPTR12 0800-1500\nEPTS05 0900-1100 1300-1600\n"
                .getBytes(StandardCharsets.UTF_8);
        r = up.upload("activations", "activations.txt", act, true, null);
        check(r.startsWith("OK END 2 airspace/activations.txt"), "activations -> " + r);

        try {
            up.upload("airspace", "zly.txt", "to nie jest OpenAir\n".getBytes(StandardCharsets.UTF_8), false, null);
            check(false, "invalid airspace rejected");
        } catch (Uploader.ProtocolException e) {
            check(e.getMessage().contains("brak stref"), "invalid airspace rejected: " + e.getMessage());
        }
        check(Files.exists(Paths.get(root, "airspace", "Polska_strefy_2026.txt")), "good file kept after a bad upload");

        // the last packet of a file is lost: no later packet reveals the gap -> timeout + resend of the window
        byte[] wpt2 = Files.readAllBytes(Paths.get(sd, "waypoints", "PRZYKLAD_punkty.cup"));
        PipeLink exact = link;
        int before = exact.dataCount;
        exact.dropAt = before + 2;  // 337 B / 240 B per packet = 2 packets: drop the 2nd (last) one
        int dropEveryWas = 0;
        up.setReplyTimeout(1500);
        r = up.upload("waypoints", "ostatni_zgubiony.cup", wpt2, false, null);
        check(r.startsWith("OK END 4 waypoints/ostatni_zgubiony.cup"), "lost last packet recovered -> " + r);
        exact.dropAt = -1;
        up.setReplyTimeout(10000);
        up.delete("waypoints/ostatni_zgubiony.cup");

        List<String> files = up.list();
        check(files.size() == 4, "LIST -> " + files);
        up.delete("waypoints/punkty.cup");
        check(!Files.exists(Paths.get(root, "waypoints", "punkty.cup")), "DEL removed the file");
        try {
            up.delete("../config.ini");
            check(false, "DEL outside allowed folders refused");
        } catch (Uploader.ProtocolException e) {
            check(true, "DEL outside allowed folders refused: " + e.getMessage());
        }
        p.getOutputStream().close();
        p.waitFor(5, TimeUnit.SECONDS);
        System.out.println(fails == 0 ? "ALL OK" : fails + " FAILED");
        System.exit(fails == 0 ? 0 : 1);
    }
}
