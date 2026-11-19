#pragma once

/* All pin numbers, timings and build options live here. */

/* ---- Antenna (XIAO ESP32-C6 RF switch) ----------------------------------
 * 0 = on-board ceramic antenna (default), 1 = external U.FL antenna.
 * GPIO3 LOW enables the switch; GPIO14 LOW = on-board, HIGH = external.
 * TODO(verify): polarity against the Seeed XIAO ESP32-C6 wiki. */
#define APP_USE_EXTERNAL_ANTENNA 0
#define APP_RF_SWITCH_ENABLE_GPIO 3
#define APP_RF_SWITCH_SELECT_GPIO 14

/* ---- Sensor pins (GPIO numbers; XIAO pad in comment) -------------------- */
#define APP_PIR1_GPIO       0   /* D0 */
#define APP_PIR2_GPIO       1   /* D1 */
#define APP_LD2410_TX_GPIO  16  /* D6: ESP TX -> LD2410C RX */
#define APP_LD2410_RX_GPIO  17  /* D7: ESP RX <- LD2410C TX */
#define APP_LD2410_BAUD     256000

/* ---- Zigbee endpoints --------------------------------------------------- */
#define APP_EP_PIR1         1   /* occupancy */
#define APP_EP_PIR2         2   /* occupancy */
#define APP_EP_LD2410       3   /* occupancy */
#define APP_EP_LD2410_BT    4   /* on/off: LD2410C Bluetooth */
#define APP_EP_LD2410_ENG   5   /* on/off: LD2410C engineering mode */

/* Zigbee strings are length-prefixed. */
#define APP_MANUFACTURER_NAME "\x03" "DIY"
#define APP_MODEL_IDENTIFIER  "\x0c" "combi-sensor"

/* Channels 11-26 */
#define APP_ZB_CHANNEL_MASK 0x07FFF800U

/* ---- Temporary bring-up aid --------------------------------------------- *
 * 1 = toggle the three occupancy endpoints every APP_DEMO_PERIOD_S seconds so
 * the Zigbee/Z2M path can be tested before the sensors are wired. Set to 0
 * once real sensors are in. */
#define APP_DEMO_MODE       1
#define APP_DEMO_PERIOD_S   20
