package pl.paranav.uploader;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.ParcelUuid;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

/**
 * BLE connection to ParaNav. All methods block and must be called from a worker thread.
 * Android allows only one outstanding GATT operation, so every call waits for its callback.
 */
@SuppressLint("MissingPermission")  // permissions are requested by MainActivity before any call
public class BleLink implements Link {
    public interface Listener {
        void onDisconnected();
    }

    private final Context ctx;
    private final BluetoothAdapter adapter;
    private final Listener listener;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic ctrl, data;
    private volatile int mtu = 23;
    private volatile boolean connected;
    private volatile int lastStatus;
    private final Semaphore opDone = new Semaphore(0);
    private final LinkedBlockingQueue<String> notifications = new LinkedBlockingQueue<>();
    private final StringBuilder lineBuf = new StringBuilder();

    public BleLink(Context ctx, BluetoothAdapter adapter, Listener listener) {
        this.ctx = ctx.getApplicationContext();
        this.adapter = adapter;
        this.listener = listener;
    }

    public boolean isConnected() {
        return connected;
    }

    // ------------------------------------------------------------------ scanning
    /** Finds a ParaNav device (by service UUID, falling back to the name). */
    public BluetoothDevice scan(long timeoutMs) throws IOException, InterruptedException {
        BluetoothLeScanner scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) throw new IOException(Tr.t("Bluetooth jest wyłączony", "Bluetooth is off"));
        final BluetoothDevice[] found = {null};
        final Semaphore done = new Semaphore(0);
        ScanCallback cb = new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult r) {
                BluetoothDevice d = r.getDevice();
                boolean byUuid = r.getScanRecord() != null && r.getScanRecord().getServiceUuids() != null
                        && r.getScanRecord().getServiceUuids().contains(new ParcelUuid(Proto.SERVICE));
                String name = r.getScanRecord() != null ? r.getScanRecord().getDeviceName() : null;
                if (byUuid || Proto.DEVICE_NAME.equals(name)) {
                    synchronized (found) {
                        if (found[0] == null) {
                            found[0] = d;
                            done.release();
                        }
                    }
                }
            }

            @Override
            public void onScanFailed(int errorCode) {
                done.release();
            }
        };
        ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
        // unfiltered scan: some phones drop 128-bit UUID filters; we match in the callback instead
        List<ScanFilter> filters = new ArrayList<>();
        scanner.startScan(filters, settings, cb);
        try {
            done.tryAcquire(timeoutMs, TimeUnit.MILLISECONDS);
        } finally {
            scanner.stopScan(cb);
        }
        synchronized (found) {
            if (found[0] == null) throw new IOException(Tr.t("nie znaleziono ParaNav – czy Telefon (Bluetooth) jest włączony?", "ParaNav not found – is Phone (Bluetooth) enabled on the device?"));
            return found[0];
        }
    }

    // ------------------------------------------------------------------ connection
    private final BluetoothGattCallback callback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                connected = true;
                opDone.release();
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                boolean was = connected;
                connected = false;
                opDone.release();  // wake up anything waiting
                if (was && listener != null) listener.onDisconnected();
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt g, int newMtu, int status) {
            if (status == BluetoothGatt.GATT_SUCCESS) mtu = newMtu;
            opDone.release();
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int status) {
            lastStatus = status;
            opDone.release();
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int status) {
            lastStatus = status;
            opDone.release();
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            lastStatus = status;
            opDone.release();
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value) {
            onNotify(value);  // Android 13+
        }

        @Override
        @SuppressWarnings("deprecation")
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            if (Build.VERSION.SDK_INT < 33) onNotify(c.getValue());  // Android 8-12
        }
    };

    private void onNotify(byte[] value) {
        if (value == null) return;
        synchronized (lineBuf) {
            lineBuf.append(new String(value, StandardCharsets.UTF_8));
            int nl;
            while ((nl = lineBuf.indexOf("\n")) >= 0) {
                String line = lineBuf.substring(0, nl).trim();
                lineBuf.delete(0, nl + 1);
                if (!line.isEmpty()) notifications.offer(line);
            }
        }
    }

    private void await(String what, long ms) throws IOException, InterruptedException {
        if (!opDone.tryAcquire(ms, TimeUnit.MILLISECONDS)) throw new IOException(Tr.t("przekroczony czas: ", "timeout: ") + what);
        if (!connected) throw new IOException(Tr.t("rozłączono", "disconnected"));
    }

    public void connect(BluetoothDevice dev) throws IOException, InterruptedException {
        close();
        opDone.drainPermits();
        gatt = dev.connectGatt(ctx, false, callback, BluetoothDevice.TRANSPORT_LE);
        if (gatt == null) throw new IOException(Tr.t("nie można połączyć", "cannot connect"));
        await(Tr.t("połączenie", "connection"), 15_000);
        gatt.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
        if (gatt.requestMtu(247)) await("MTU", 5_000);
        if (!gatt.discoverServices()) throw new IOException(Tr.t("nie można odczytać usług", "cannot read services"));
        await(Tr.t("usługi", "services"), 10_000);
        BluetoothGattService svc = gatt.getService(Proto.SERVICE);
        if (svc == null) throw new IOException(Tr.t("to urządzenie nie ma usługi ParaNav (stary firmware?)", "this device has no ParaNav service (old firmware?)"));
        ctrl = svc.getCharacteristic(Proto.CTRL);
        data = svc.getCharacteristic(Proto.DATA);
        if (ctrl == null || data == null) throw new IOException(Tr.t("niekompletna usługa ParaNav", "incomplete ParaNav service"));
        gatt.setCharacteristicNotification(ctrl, true);
        BluetoothGattDescriptor cccd = ctrl.getDescriptor(Proto.CCCD);
        if (cccd == null) throw new IOException(Tr.t("brak powiadomień w usłudze", "no notifications in the service"));
        writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
        await(Tr.t("powiadomienia", "notifications"), 5_000);
    }

    @SuppressWarnings("deprecation")
    private void writeDescriptor(BluetoothGattDescriptor d, byte[] v) throws IOException {
        if (Build.VERSION.SDK_INT >= 33) {
            if (gatt.writeDescriptor(d, v) != BluetoothStatusCodes.SUCCESS) throw new IOException(Tr.t("zapis deskryptora", "descriptor write"));
        } else {
            d.setValue(v);
            if (!gatt.writeDescriptor(d)) throw new IOException(Tr.t("zapis deskryptora", "descriptor write"));
        }
    }

    public void close() {
        connected = false;
        if (gatt != null) {
            try {
                gatt.disconnect();
                gatt.close();
            } catch (Exception ignored) {
            }
            gatt = null;
        }
    }

    // ------------------------------------------------------------------ Link
    @SuppressWarnings("deprecation")
    private void write(BluetoothGattCharacteristic c, byte[] v, int type) throws IOException, InterruptedException {
        if (gatt == null || !connected) throw new IOException(Tr.t("brak połączenia", "not connected"));
        opDone.drainPermits();
        for (int attempt = 0; ; attempt++) {
            boolean started;
            if (Build.VERSION.SDK_INT >= 33) {
                started = gatt.writeCharacteristic(c, v, type) == BluetoothStatusCodes.SUCCESS;
            } else {
                c.setWriteType(type);
                c.setValue(v);
                started = gatt.writeCharacteristic(c);
            }
            if (started) break;
            if (attempt > 50) throw new IOException(Tr.t("Bluetooth zajęty", "Bluetooth busy"));
            Thread.sleep(10);  // stack busy: try again
        }
        await(Tr.t("zapis", "write"), 5_000);
        if (lastStatus != BluetoothGatt.GATT_SUCCESS) throw new IOException(Tr.t("błąd zapisu ", "write error ") + lastStatus);
    }

    @Override
    public void sendCtrl(String line) throws IOException, InterruptedException {
        write(ctrl, (line + "\n").getBytes(StandardCharsets.UTF_8), BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
    }

    @Override
    public void sendData(byte[] packet) throws IOException, InterruptedException {
        write(data, packet, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
    }

    @Override
    public String waitNotify(long timeoutMs) throws InterruptedException {
        return notifications.poll(timeoutMs, TimeUnit.MILLISECONDS);
    }

    @Override
    public void clearNotifications() {
        notifications.clear();
    }

    @Override
    public int maxPacket() {
        return mtu - 3;
    }
}
