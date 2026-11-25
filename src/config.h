// Board pins and firmware constants for LilyGO T5 E-Paper S3 Pro (H752).
// Source: LilyGO repo docs/pinmap.md (schematic V1.0 24-12-24).
#pragma once

#define PARANAV_VERSION "0.5.11"

// Shared I2C bus (PCA9535, PCF8563, BQ27220, GT911, TPS65185, BQ25896 and the external MS5611 via QWIIC)
#define PIN_I2C_SDA 39
#define PIN_I2C_SCL 40

// GNSS (MIA-M10Q or L76K) on UART, power shared with LoRa via PCA9535 IO0_0 (raised by epdiy board init)
#define PIN_GPS_RX 44  // ESP32 RX  <- GPS TX
#define PIN_GPS_TX 43  // ESP32 TX  -> GPS RX

// SPI bus shared by SD card and LoRa
#define PIN_SPI_SCK 14
#define PIN_SPI_MISO 21
#define PIN_SPI_MOSI 13
#define PIN_SD_CS 12
#define PIN_LORA_CS 46    // SX1262 LoRa (FANET), shares SPI with the SD card
#define PIN_LORA_IRQ 10   // DIO1
#define PIN_LORA_RST 1
#define PIN_LORA_BUSY 47

// Touch GT911
#define PIN_TOUCH_INT 3
#define PIN_TOUCH_RST 9

// Buttons
#define PIN_BOOT_BTN 0          // GPIO0, active low
#define PCA9535_ADDR 0x20
#define PCA9535_BTN_PORT 1      // input port 1
#define PCA9535_BTN_MASK 0x04   // IO1_2 = on-board user button "S3", active low

// MS5611 I2C address: 0x77 when CSB low (GY-63 default) or 0x76 - both are probed
#define MS5611_ADDR_A 0x77
#define MS5611_ADDR_B 0x76

// Front light (not used, kept off)
#define PIN_BL_EN 11

// Touch coordinate mapping from GT911 raw (portrait, as used by the LilyGO factory demo)
// to the native landscape frame. Verify on SYSTEM screen ("Dotyk (test)") and adjust if needed.
// native_x = TOUCH_X_FROM_RAW_Y ? raw_y : raw_x ... see Board::touchToScreen()
#define TOUCH_RAW_PORTRAIT 1

// Paths on the SD card (mounted at /sd)
#define SD_MOUNT "/sd"
#define PATH_CONFIG SD_MOUNT "/config.ini"
#define PATH_AIRSPACE SD_MOUNT "/airspace"
#define PATH_ACTIVATIONS SD_MOUNT "/airspace/activations.txt"
#define PATH_WAYPOINTS SD_MOUNT "/waypoints"
#define PATH_ROUTE SD_MOUNT "/route"
#define PATH_TERRAIN SD_MOUNT "/terrain"
#define PATH_MAPS SD_MOUNT "/maps"
#define PATH_LOGS SD_MOUNT "/logs"

// Timing
#define LONG_PRESS_MS 700
#define POWER_ARM_MS 1000   // both buttons: show power-off hint
#define POWER_HOLD_MS 3000  // both buttons: power off
#define UI_PERIOD_MS 1000
