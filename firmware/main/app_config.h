#pragma once

/* All pin numbers, timings and build options live here. */

/* ---- Antenna (XIAO ESP32-C6 RF switch) ----------------------------------
 * 0 = on-board ceramic antenna (default), 1 = external U.FL antenna.
 * GPIO3 LOW enables the switch; GPIO14 LOW = on-board, HIGH = external.
 * Polarity verified against the Seeed XIAO ESP32-C6 wiki (wiki.seeedstudio.com/xiao_esp32c6_getting_started). */
#define APP_USE_EXTERNAL_ANTENNA 1
#define APP_RF_SWITCH_ENABLE_GPIO 3
#define APP_RF_SWITCH_SELECT_GPIO 14

/* ---- Sensor pins (GPIO numbers; XIAO pad in comment) -------------------- */
#define APP_PIR1_GPIO       0   /* D0 */
#define APP_PIR2_GPIO       1   /* D1 */
#define APP_PIR_POLL_MS     20  /* PIR sampling period */
#define APP_PIR_DEBOUNCE_MS 100 /* level must be stable this long */
#define APP_LD2410_TX_GPIO  16  /* D6: ESP TX -> LD2410C RX */
#define APP_LD2410_RX_GPIO  17  /* D7: ESP RX <- LD2410C TX */
#define APP_LD2410_BAUD     256000
#define APP_LD2410_OFF_DELAY_MS      1000  /* hold "present" this long after the radar sees nobody */
#define APP_LD2410_DATA_TIMEOUT_MS  10000  /* no frames for this long -> report no presence */
#define APP_LD2410_ACK_TIMEOUT_MS     500  /* wait for a command acknowledgement */

/* ---- Pairing reset button + status LED ------------------------------------
 * Holding the BOOT button this long (while running) leaves the Zigbee network and clears pairing.
 * The LED blinks once the press passes APP_BUTTON_WARN_MS. Do not hold BOOT while resetting or
 * powering up: that enters the ROM bootloader instead. */
#define APP_BUTTON_GPIO     9    /* BOOT button, active low */
#define APP_BUTTON_WARN_MS  2000
#define APP_BUTTON_HOLD_MS  5000
#define APP_STATUS_LED_GPIO 15   /* user LED. TODO(verify): active low on the XIAO ESP32-C6 */
#define APP_STATUS_LED_ACTIVE_LOW 1

/* ---- Zigbee endpoints --------------------------------------------------- */
#define APP_EP_PIR1         1   /* occupancy */
#define APP_EP_PIR2         2   /* occupancy */
#define APP_EP_LD2410       3   /* occupancy */
#define APP_EP_LD2410_BT    4   /* on/off: LD2410C Bluetooth */
#define APP_EP_LD2410_ENG   5   /* on/off: LD2410C engineering mode */

/* Zigbee strings are length-prefixed. */
#define APP_MANUFACTURER_NAME "\x03" "DIY"
#define APP_MODEL_IDENTIFIER  "\x0c" "combi-sensor"

/* State refresh towards the coordinator: once shortly after every join/rejoin, then periodically.
 * Keeps Z2M correct after a rejoin without a manual Reconfigure (5 small frames per period). */
#define APP_JOIN_REPORT_DELAY_MS 3000
#define APP_HEARTBEAT_S          600

/* Channels 11-26 */
#define APP_ZB_CHANNEL_MASK 0x07FFF800U

/* ---- Temporary bring-up aid --------------------------------------------- *
 * 1 = toggle the LD2410C occupancy endpoint (EP3) every APP_DEMO_PERIOD_S seconds so
 * the Zigbee/Z2M path can be tested before the radar driver exists. Set to 0 once
 * the LD2410C is wired and its driver is in. */
#define APP_DEMO_MODE       0
#define APP_DEMO_PERIOD_S   20
