package pl.paranav.uploader;

import java.util.UUID;

/** Constants shared with the firmware (src/phone_ble.h, lib/navcore/src/filexfer.h). */
public final class Proto {
    private Proto() {}

    public static final UUID SERVICE = UUID.fromString("7e2a0001-5a1d-4c3b-9f0e-3a8d2c6b1e01");
    public static final UUID CTRL = UUID.fromString("7e2a0002-5a1d-4c3b-9f0e-3a8d2c6b1e01");
    public static final UUID DATA = UUID.fromString("7e2a0003-5a1d-4c3b-9f0e-3a8d2c6b1e01");
    /** Standard Client Characteristic Configuration descriptor (enables notifications). */
    public static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    public static final String KIND_AIRSPACE = "airspace";
    public static final String KIND_ACTIVATIONS = "activations";
    public static final String KIND_WAYPOINTS = "waypoints";
    public static final String KIND_ROUTE = "route";

    public static final int MAX_FILE_SIZE = 8 * 1024 * 1024;
    public static final String DEVICE_NAME = "ParaNav";
}
