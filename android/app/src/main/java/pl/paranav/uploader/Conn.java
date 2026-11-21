package pl.paranav.uploader;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** The Bluetooth connection to ParaNav, shared by all screens of the app. */
final class Conn {
    private Conn() {}

    static volatile BleLink link;
    static volatile Uploader uploader;
    static volatile String banner = "";
    /** All BLE work runs here, one job at a time. */
    static final ExecutorService worker = Executors.newSingleThreadExecutor();

    static boolean connected() {
        BleLink l = link;
        return l != null && l.isConnected() && uploader != null;
    }

    /** Firmware version as a number (0.5.0 -> 500). */
    static int firmware() {
        return Uploader.firmwareVersion(banner);
    }

    /** "Set as active route" needs firmware 0.5 or newer (older firmware would read the flag as "replace"). */
    static boolean canActivateRoute() {
        return firmware() >= 500;
    }

    static void close() {
        BleLink l = link;
        if (l != null) l.close();
        link = null;
        uploader = null;
        banner = "";
    }
}
