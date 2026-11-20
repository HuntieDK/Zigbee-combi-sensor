# combi-sensor

A Zigbee presence sensor for the [Seeed Studio XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/),
built with ESP-IDF and `esp-zigbee-lib`. It combines three sensors in one always-on Zigbee end device
and shows up in Home Assistant through [Zigbee2MQTT](https://www.zigbee2mqtt.io/).

## What it does

| Home Assistant entity | Source |
|---|---|
| Occupancy (PIR 1) | HC-SR501-type PIR on D0 |
| Occupancy (PIR 2) | HC-SR501-type PIR on D1 |
| Occupancy (radar) | HLK-LD2410C presence flag |
| Switch: radar Bluetooth | turns the LD2410C's Bluetooth on or off |
| Switch: radar engineering mode | turns the LD2410C's engineering mode on or off |

It is not a router, needs no Wi-Fi, and keeps Zigbee traffic low: it reports only on changes, plus a
state refresh after each join and every 10 minutes.

## Hardware

| Part | Connection |
|---|---|
| PIR 1 output | D0 (GPIO0) |
| PIR 2 output | D1 (GPIO1) |
| LD2410C TX | D7 (GPIO17, ESP RX) |
| LD2410C RX | D6 (GPIO16, ESP TX) |
| LD2410C and PIR power | 5V pad and GND |

The LD2410C OUT pin is not used. The external antenna (U.FL) or the on-board antenna is chosen with
`APP_USE_EXTERNAL_ANTENNA` in `firmware/main/app_config.h`.

## Build and flash

Requires ESP-IDF v5.5.x (developed with v5.5.6).

```sh
. ~/esp/esp-idf/export.sh
cd firmware
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash
```

Run the host-side unit tests (no ESP-IDF needed): `firmware/test/host/run.sh`

## Zigbee2MQTT

Copy `z2m/combi-sensor.mjs` into the `external_converters/` folder of your Zigbee2MQTT data directory and
restart Zigbee2MQTT. Then enable permit-join and power on the device. See [z2m/README.md](z2m/README.md).
Tested with Zigbee2MQTT 2.12.1 and a Sonoff ZBDongle-P coordinator.

To remove the pairing, hold the BOOT button for 5 seconds. The LED blinks after 2 seconds and goes
solid at 5. The device then looks for a new network.

## Repository layout

- `firmware/` ESP-IDF project, with pure-C parsers and filters tested on the host in `firmware/test/host/`
- `z2m/` Zigbee2MQTT external converter
- `docs/` notes on using the XIAO ESP32-C6 as a Zigbee device
- `planning/` the original design plan

## Status

Tested on real hardware: Zigbee join and rejoin, the LD2410C presence and both switches, and the
pairing-reset button. The PIR inputs have been tested by driving the pins directly; HC-SR501 sensors are
not yet wired. The external antenna is selected and connected, but its range has not been compared with
the on-board antenna. Long-term stability testing is just starting.

## License

[0BSD](LICENSE.md): use it for anything, no attribution needed, no warranty. Use at your own risk.
The dependencies (ESP-IDF, esp-zigbee-lib) keep their own licenses.
