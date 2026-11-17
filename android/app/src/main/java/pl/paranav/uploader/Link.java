package pl.paranav.uploader;

import java.io.IOException;

/** Transport used by {@link Uploader}: BLE on the phone, a pipe to the simulator in tests. */
public interface Link {
    /** Send one control line (without '\n'); blocks until the write is confirmed. */
    void sendCtrl(String line) throws IOException, InterruptedException;

    /** Send one binary data packet; blocks until the BLE stack accepted it. */
    void sendData(byte[] packet) throws IOException, InterruptedException;

    /** Next notification line from the device, or null after timeoutMs. */
    String waitNotify(long timeoutMs) throws InterruptedException;

    /** Drop notifications that arrived before a new command. */
    void clearNotifications();

    /** Maximum size of one data packet (ATT MTU - 3). */
    int maxPacket();
}
