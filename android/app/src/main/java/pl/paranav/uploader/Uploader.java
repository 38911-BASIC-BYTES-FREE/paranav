package pl.paranav.uploader;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.CRC32;

/**
 * File upload protocol, phone side (pure Java - no Android classes, tested on the desktop
 * against the firmware code in tools/android_proto_test).
 */
public class Uploader {
    public interface Progress {
        void onProgress(long done, long total);
    }

    /** Device answered with an error or did not answer. */
    public static class ProtocolException extends IOException {
        private static final long serialVersionUID = 1L;

        public ProtocolException(String msg) {
            super(msg);
        }
    }

    private long REPLY_TIMEOUT_MS = 10_000;
    private static final long INSTALL_TIMEOUT_MS = 180_000;  // parsing a big airspace file takes a while
    private final Link link;

    public Uploader(Link link) {
        this.link = link;
    }

    /** For tests: shorter timeouts. */
    public void setReplyTimeout(long ms) {
        REPLY_TIMEOUT_MS = ms;
    }

    /** Returns the device banner, e.g. "PARANAV 1 0.1.0". */
    public String hello() throws IOException, InterruptedException {
        link.clearNotifications();
        link.sendCtrl("HELLO");
        return expect("PARANAV", REPLY_TIMEOUT_MS);
    }

    /**
     * Uploads a file. Returns the device's final message, e.g. "OK END 812 airspace/pl.txt".
     *
     * @param kind one of Proto.KIND_*
     */
    public String upload(String kind, String name, byte[] data, boolean replace, Progress progress)
            throws IOException, InterruptedException {
        if (data.length == 0) throw new ProtocolException(Tr.t("plik jest pusty", "the file is empty"));
        if (data.length > Proto.MAX_FILE_SIZE) throw new ProtocolException(Tr.t("plik za duży (max 8 MB)", "file too large (max 8 MB)"));
        CRC32 crc = new CRC32();
        crc.update(data);
        String safeName = name.replace('\n', ' ').replace('\r', ' ').trim();
        link.clearNotifications();
        link.sendCtrl(String.format(Locale.ROOT, "BEGIN %s %d %08x %d %s", kind, data.length, crc.getValue(),
                replace ? 1 : 0, safeName));
        String begin = expect("OK BEGIN", REPLY_TIMEOUT_MS);
        long window = Long.parseLong(begin.substring("OK BEGIN".length()).trim());
        int payload = Math.max(16, link.maxPacket() - 4);

        long sent = 0, acked = 0;
        int silent = 0;
        while (acked < data.length) {
            // keep at most one window of unacknowledged data in flight
            while (sent < data.length && sent - acked < window) {
                int len = (int) Math.min(payload, data.length - sent);
                byte[] pkt = new byte[4 + len];
                pkt[0] = (byte) sent;
                pkt[1] = (byte) (sent >>> 8);
                pkt[2] = (byte) (sent >>> 16);
                pkt[3] = (byte) (sent >>> 24);
                System.arraycopy(data, (int) sent, pkt, 4, len);
                link.sendData(pkt);
                sent += len;
            }
            String n = link.waitNotify(REPLY_TIMEOUT_MS);
            if (n == null) {
                // probably the last packet of a window got lost: send the window again from the last ACK
                if (++silent > 3) {
                    abortQuietly();
                    throw new ProtocolException(Tr.t("urządzenie nie odpowiada", "the device does not respond"));
                }
                sent = acked;
                continue;
            }
            silent = 0;
            if (n.startsWith("ACK ")) {
                acked = Math.max(acked, Long.parseLong(n.substring(4).trim()));
                if (progress != null) progress.onProgress(acked, data.length);
            } else if (n.startsWith("ERR SEQ ")) {
                // a packet was lost: continue from where the device is
                long at = Long.parseLong(n.substring(8).trim());
                sent = at;
                acked = at;
            } else if (n.startsWith("ERR")) {
                throw new ProtocolException(n);
            }
        }
        link.sendCtrl("END");
        expect("OK RECEIVED", REPLY_TIMEOUT_MS * 3);
        return expect("OK END", INSTALL_TIMEOUT_MS);
    }

    /** Lists files in /airspace, /waypoints and /route as "dir/name size". */
    public List<String> list() throws IOException, InterruptedException {
        link.clearNotifications();
        link.sendCtrl("LIST");
        List<String> out = new ArrayList<>();
        while (true) {
            String n = link.waitNotify(REPLY_TIMEOUT_MS);
            if (n == null) throw new ProtocolException(Tr.t("urządzenie nie odpowiada", "the device does not respond"));
            if (n.startsWith("FILE ")) out.add(n.substring(5));
            else if (n.startsWith("OK LIST")) return out;
            else if (n.startsWith("ERR")) throw new ProtocolException(n);
        }
    }

    /** Deletes a file, path as returned by list() without the size, e.g. "waypoints/a.cup". */
    public void delete(String path) throws IOException, InterruptedException {
        link.clearNotifications();
        link.sendCtrl("DEL " + path);
        expect("OK DEL", REPLY_TIMEOUT_MS);
        expect("OK RELOADED", INSTALL_TIMEOUT_MS);
    }

    private void abortQuietly() {
        try {
            link.sendCtrl("ABORT");
        } catch (Exception ignored) {
        }
    }

    /** Waits for a line starting with prefix; an "ERR ..." line fails immediately. */
    private String expect(String prefix, long timeoutMs) throws IOException, InterruptedException {
        long end = System.currentTimeMillis() + timeoutMs;
        while (true) {
            long left = end - System.currentTimeMillis();
            String n = left > 0 ? link.waitNotify(left) : null;
            if (n == null) throw new ProtocolException(Tr.t("brak odpowiedzi (", "no response (") + prefix + ")");
            if (n.startsWith(prefix)) return n;
            if (n.startsWith("ERR SEQ")) continue;  // late message from the data phase
            if (n.startsWith("ERR")) throw new ProtocolException(describe(n));
            // ACK and other late lines are ignored
        }
    }

    /** Human readable text for device error codes. */
    public static String describe(String err) {
        if (err.startsWith("ERR CRC")) return Tr.t("błąd sumy kontrolnej – spróbuj ponownie", "checksum error – try again");
        if (err.startsWith("ERR SIZE")) return Tr.t("zły rozmiar pliku", "bad file size");
        if (err.startsWith("ERR SD")) return Tr.t("błąd karty SD w urządzeniu", "SD card error on the device");
        if (err.startsWith("ERR KIND")) return Tr.t("nieznany rodzaj pliku", "unknown file type");
        if (err.startsWith("ERR PATH")) return Tr.t("niedozwolona ścieżka", "path not allowed");
        if (err.startsWith("ERR ")) return err.substring(4);
        return err;
    }

    static byte[] utf8(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }
}
