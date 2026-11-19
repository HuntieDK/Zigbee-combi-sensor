# Combi-sensor: XIAO ESP32-C6 Zigbee device — plan

Status: DRAFT for review. No firmware has been written yet.

## 1. Goal

A mains/USB-powered Zigbee **end device** (not a router) on a XIAO ESP32-C6 that
reports to Home Assistant via Zigbee2MQTT (Z2M):

| # | Sensor | HA entity |
|---|--------|-----------|
| 1 | PIR 1 (HC-SR501 type) | `binary_sensor` occupancy, "present" |
| 2 | PIR 2 (HC-SR501 type) | `binary_sensor` occupancy, "present" |
| 3 | HLK-LD2410C | `binary_sensor` occupancy (overall presence flag) |
| 3b | LD2410C Bluetooth | `switch` (control) |
| 3c | LD2410C engineering mode | `switch` (control) |
| 3d (later) | LD2410C still / moving energy | `sensor` (0–100 %) |

Non-goals (rev 1): router role, battery/sleep operation, OTA, per-gate energy data.

## 2. Decisions made so far

| Topic | Decision |
|-------|----------|
| Framework | ESP-IDF + `esp-zigbee-sdk` (Espressif's official stack; ESP32-C6 has native 802.15.4) |
| Power | USB/mains 5 V, always on → non-sleepy end device (`ZB_ED`, rx-on-when-idle) |
| Z2M | HA add-on, currently 2.12.1; user manages/upgrades it. Coordinator: Sonoff ZBDongle-P ("Dongle Plus-P", TI CC2652P, Z-Stack) |
| Antenna | Selectable at build time via `APP_USE_EXTERNAL_ANTENNA` in `app_config.h` (0 = on-board ceramic, 1 = external U.FL). **Start with on-board (0)**; external is a later option (see §3) |
| LD2410C OUT pin | Not wired; presence comes from UART only |
| PIRs | HC-SR501-type (3.3 V digital output) |
| Dev env | WSL2 build + `usbipd-win` to attach the board for flashing/monitor |

## 3. Pin allocation (proposal)

XIAO ESP32-C6 pads used (verify against the Seeed pinout when wiring):

| Function | XIAO pad | GPIO | Notes |
|----------|----------|------|-------|
| LD2410C **RX** ← ESP TX | D6 | GPIO16 | UART1 TX (via GPIO matrix) |
| LD2410C **TX** → ESP RX | D7 | GPIO17 | UART1 RX |
| PIR 1 OUT | D0 | GPIO0 | Input, no pull needed (HC-SR501 drives it) |
| PIR 2 OUT | D1 | GPIO1 | Input |
| RF switch power | – | GPIO3 | Output LOW to enable the antenna switch (internal, not on a pad) |
| Antenna select | – | GPIO14 | Output LOW = on-board ceramic antenna, HIGH = external U.FL antenna |
| 5 V | 5V pad | – | Feeds LD2410C VCC and both PIR VCC (HC-SR501 needs 4.5–20 V) |
| GND | GND | – | Common ground |

Avoid: GPIO9 (BOOT strap), GPIO12/13 (USB),
GPIO15 (user LED), GPIO4–7 (JTAG/strapping area).

Electrical checks:
- LD2410C UART lines are 3.3 V logic: connect directly, no level shifter.
- LD2410C draws ~80 mA at 5 V (peaks higher) — supply from USB 5 V, not the 3.3 V pin.
- HC-SR501 output is ~3.3 V: OK for the C6 inputs. Confirm with a multimeter before connecting.
- **Antenna selection**: controlled by `APP_USE_EXTERNAL_ANTENNA` in `app_config.h` (default `0`, on-board). Firmware always drives the RF switch explicitly early in `app_main`, before Zigbee starts: GPIO3 LOW (enable switch), then GPIO14 LOW for on-board or HIGH for external (verify polarity against the Seeed XIAO ESP32-C6 wiki). When external is selected, attach the antenna before powering.
- Console/logging uses the built-in USB-Serial-JTAG, so UART1 on D6/D7 is free for the LD2410C.

## 4. Zigbee design

Device type: end device. One device, 5 endpoints in rev 1 (+2 later).

| EP | Device type | Clusters (server) | Purpose |
|----|-------------|-------------------|---------|
| 1 | Occupancy Sensor | Basic, Identify, Occupancy Sensing (0x0406) | PIR 1 |
| 2 | Occupancy Sensor | same | PIR 2 |
| 3 | Occupancy Sensor | same | LD2410C presence |
| 4 | On/Off Output | On/Off (0x0006) | LD2410C Bluetooth switch |
| 5 | On/Off Output | On/Off | LD2410C engineering-mode switch |
| 6 (later) | Analog Input | Analog Input (0x000C) | Moving energy |
| 7 (later) | Analog Input | Analog Input | Still energy |

Notes:
- Occupancy attribute 0x0000 bit 0 = present. Use attribute reporting
  (min/max interval) and also send a report on every state change.
- Multi-endpoint requires a Z2M **external converter** (`z2m/combi-sensor.mjs`, copied to the Z2M `external_converters` folder)
  that names endpoints (`pir1`, `pir2`, `ld2410`, …) and maps them to HA entities.
- Basic cluster: set a unique manufacturer name / model id so Z2M matches the converter.
- Join: use network steering on first boot / after factory-reset button (BOOT button
  long-press → leave network + NVS wipe). Report join state on the on-board LED.
- Not a router: build as `ESP_ZB_DEVICE_TYPE_ED`, no routing table duties.

### Avoiding Zigbee network flooding
- Occupancy: event-driven, only on change; additionally a slow heartbeat (e.g. 5–10 min).
- PIR debouncing in firmware (HC-SR501 has its own hold time; we add ~100 ms debounce
  and don't send duplicate states).
- LD2410C emits data frames ~10 Hz — **never forward these directly**. Firmware
  keeps the latest values and only reports on state change.
- Energy levels (phase 2): report with min interval ≥ 30 s, max interval ≥ 10 min,
  reportable change ≥ 10 %. Optionally off by default and enabled in Z2M.
- Cap total reports to a global rate limit (e.g. ≤ 1 report/s device-wide).

## 5. LD2410C interface

- UART 256000 baud, 8N1 (module default). Frame header `FD FC FB FA`, footer `04 03 02 01`
  for commands; data frames use `F4 F3 F2 F1` … `F8 F7 F6 F5`.
- Presence: target-state byte in the basic data frame (0 = none, 1 = moving,
  2 = still, 3 = both). "Present" = state ≠ 0. Apply a short hold/off delay in firmware
  to avoid flapping.
- Control commands (each wrapped in enable-config `0xFF` … end-config `0xFE`):
  - Engineering mode on/off: `0x62` / `0x63`.
  - Bluetooth on/off: `0xA4`, then restart module `0xA3` for it to take effect.
- State of the switches: the module can't be reliably queried for Bluetooth state, so
  firmware stores last commanded value in NVS and restores it on boot (re-sends the
  command). Engineering mode is lost on module power cycle, so re-apply after boot.
- Engineering-mode frames carry per-gate energies; in phase 2 we derive aggregate
  moving/still energy from the basic frame (moving energy, still energy bytes), which
  is available even without engineering mode.

## 6. Repository layout (proposed)

```
planning/                       this plan
firmware/                       ESP-IDF project
  main/
    main.c                      app init, Zigbee start, task wiring
    zigbee_device.c/.h          endpoints, clusters, attribute updates, join logic
    pir.c/.h                    GPIO ISR + debounce → events
    ld2410.c/.h                 UART driver, frame parser, command sender
    app_config.h                pin numbers, timings, rate limits
  partitions.csv                zigbee + zb_fct partitions
  sdkconfig.defaults
z2m/
  combi-sensor.mjs              external converter (ESM, Z2M 2.x)
docs/                           wiring, setup notes
```

## 7. Toolchain setup (WSL2)

1. **WSL2 packages**: `git wget flex bison gperf python3 python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0`.
2. **ESP-IDF**: clone a current stable release (v5.4.x or newer; confirm the version
   `esp-zigbee-sdk` currently requires), run `install.sh esp32c6`, `source export.sh`.
3. **Zigbee components**: add via IDF component manager in `main/idf_component.yml`:
   `espressif/esp-zigbee-lib` and `espressif/esp-zboss-lib` (pin versions once chosen).
4. **USB passthrough**: install `usbipd-win` on Windows; `usbipd list`,
   `usbipd bind --busid <id>`, `usbipd attach --wsl --busid <id>`. Board then appears as
   `/dev/ttyACM0`. Add the user to the `dialout` group. Re-attach after each re-enumeration
   (the C6 re-enumerates on reset into the bootloader).
5. **First test**: build the esp-zigbee-sdk `HA_on_off_light`/end-device example for
   `esp32c6`, flash, confirm it joins in Z2M. Do this before any custom code.
6. VS Code: ESP-IDF extension optional; CLI (`idf.py`) is the baseline.

## 8. Implementation phases

1. **Bring-up**: toolchain, blink, flash over usbipd, serial monitor.
2. **Zigbee skeleton**: single occupancy endpoint, joins Z2M, toggled by a timer. Write
   the external converter against it.
3. **PIRs**: add EP1/EP2 with GPIO, debounce, report on change. Verify in HA.
4. **LD2410C read-only**: UART parser; log frames; unit-test parser on the host with
   captured frames. EP3 presence reporting.
5. **LD2410C control**: switches EP4/EP5, command sender with ack parsing, NVS persistence.
6. **Hardening**: rate limiter, heartbeat, factory-reset button, watchdog, reconnect
   behaviour (parent loss / rejoin), long soak test (24 h) watching Z2M for traffic.
7. **Phase 2**: moving/still energy as analog inputs with conservative reporting.

## 9. Testing

- Host-side unit tests (Unity/pytest) for the LD2410 frame parser and debounce logic.
- On-device: Z2M log + `mosquitto_sub` to confirm message rate stays low.
- Check Z2M "Network map"/LQI and that there are no repeated retransmits.

## 10. Risks / open questions

- **Bluetooth switch semantics**: module restart is required after toggling; confirm
  behaviour on the actual unit/firmware version. Decide whether it's a persistent
  setting (stored on module) or re-applied by us on boot.
- **Z2M converter**: Z2M 2.x loads external converters/definitions as ES modules (`.mjs`) from `external_converters/` in the Z2M data dir (HA add-on: `/config/zigbee2mqtt/external_converters/`). The converter API changes between Z2M releases; since the user manages Z2M updates, the converter is written against 2.12.1 and revalidated after upgrades.
- **esp-zigbee-sdk versions** change quickly; pin exact component versions after the first
  working build.
- **Coordinator**: ZBDongle-P (CC2652P) is well supported by Z2M's `zstack` driver; use a USB extension cable away from USB3 ports. No Wi-Fi on the device, so no coexistence concern. If the external antenna is used, keep it clear of the LD2410C and metal.
- **HC-SR501 power/noise**: the PIR is sensitive to supply ripple from the radar module;
  add a 100 µF capacitor on the 5 V rail if false triggers appear.
- No open questions for you at this point; remaining unknowns are hardware behaviours to verify during bring-up.
