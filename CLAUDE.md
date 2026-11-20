# CLAUDE.md

Firmware for a Zigbee "combi-sensor" on a Seeed XIAO ESP32-C6: two PIR inputs plus an
HLK-LD2410C radar, exposed through Zigbee2MQTT (Z2M) to Home Assistant.
The approved design is in `planning/zigbee-combi-sensor-plan.md` — read it first and keep
it updated when decisions change. Reusable Zigbee/Z2M/toolchain findings (for this and future devices)
are in `docs/zigbee-device-context.md`; keep it updated when you learn something new. License: 0BSD
(`LICENSE.md`); public overview in `README.md`.

## Status

Planning done, toolchain verified, Z2M converter (`z2m/combi-sensor.mjs`) working: the device is
supported and reconfigured in Z2M. `firmware/` builds, runs and rejoins the network after reflash:
antenna select, Zigbee end device (3 occupancy + 2 On/Off endpoints), PIR polling driver with a
host-tested debouncer, LD2410C protocol parser and presence filter (`./firmware/test/host/run.sh`).
`APP_DEMO_MODE` in `app_config.h` is now off (it only toggles EP3 for bring-up).
LD2410C verified on hardware: presence (Z2M occupancy_radar follows the room), engineering-mode
switch, and Bluetooth switch (module restart, ~2 s, state read back) all work.
Not yet done: real HC-SR501 PIRs wired, LQI comparison on the external antenna (on-board readings
varied 65-196 depending on placement), long soak test, `docs/`, still/moving energy sensors (phase 2).
Known harmless Z2M log noise: after a reconnect Z2M reads genOnOff on EP1 (UNSUPPORTED_CLUSTER).
Layout is in the plan §6; `main/zigbee_device.c` is the Zigbee API wrapper.

## Key decisions (see plan for detail)

- ESP-IDF **v5.5.6** + `espressif/esp-zigbee-lib` (managed component), target `esp32c6`.
- Always-on, USB-powered Zigbee **end device** (`ZB_ED`), never a router. No Wi-Fi, no sleep.
- Endpoints: EP1/EP2 PIR occupancy, EP3 LD2410C occupancy, EP4 Bluetooth switch,
  EP5 engineering-mode switch; EP6/EP7 still/moving energy later (analog input).
- Pins: LD2410C TX→GPIO17 (D7), RX←GPIO16 (D6), UART1; PIR1 GPIO0 (D0), PIR2 GPIO1 (D1).
  LD2410C OUT pin is **not wired**.
- Antenna is a build-time option `APP_USE_EXTERNAL_ANTENNA` in `app_config.h` (0 = on-board,
  1 = external U.FL; currently 1, external antenna attached). Firmware always sets GPIO3 LOW, waits
  100 ms, then sets GPIO14 (LOW on-board / HIGH external) before Zigbee starts. Polarity and all pin
  numbers (D0-D7, BOOT=GPIO9, LED=GPIO15, RF switch GPIO3/14) verified against the Seeed wiki.
- Pairing reset: hold the BOOT button (GPIO9) for 5 s while running (LED on GPIO15 blinks after 2 s);
  it calls `ezb_bdb_reset_via_local_action()`, leaves the network, wipes pairing and starts looking
  for a new one (needs permit-join). Never hold BOOT during reset/power-up (enters bootloader).
  Verified on hardware: LED blinks after 2 s and goes solid at 5 s (active-low LED confirmed), board
  leaves and then re-pairs from scratch after deleting it in Z2M (clean entities, no manual Reconfigure).
- Z2M is the HA add-on (2.12.1), managed by the user. Multi-endpoint support needs an
  external converter written as an ES module (`z2m/combi-sensor.mjs`).
- Coordinator: Sonoff ZBDongle-P (CC2652P).

## Environment

- Dev host: Ubuntu 26.04 on WSL2, Python 3.14. Commands run as user `sjh` (not root).
  Do not create root-owned files in the repo, `~/esp` or `~/.espressif`.
- ESP-IDF lives in `~/esp/esp-idf`; tools in `~/.espressif`. Activate with:
  ```
  export IDF_TOOLS_PATH=$HOME/.espressif
  . ~/esp/esp-idf/export.sh
  ```
  If `export.sh` fails with a permission error on `/tmp/esp_idf_activate_sjh`, that directory
  is a root-owned leftover: remove it with sudo, or set `TMPDIR` to a directory you own.
- Build from `firmware/`: `idf.py set-target esp32c6`, `idf.py build`.
- Flashing needs the board attached to WSL via `usbipd-win` (installed on Windows, 5.3.0):
  `usbipd list`, `usbipd bind --busid <id>`, `usbipd attach --wsl --busid <id>` in an admin
  PowerShell on Windows; the board then appears as `/dev/ttyACM0`. The C6 re-enumerates
  when it resets into the bootloader, so it may need re-attaching.
- Toolchain smoke test: the SDK's `examples/home_automation_devices/on_off_light` builds for
  esp32c6 (verified). Keep scratch clones of external SDKs out of the repo.

## Lessons from toolchain bring-up

- Verified: the SDK `temperature_sensor` example (end device) flashed to the XIAO and joined
  Z2M (2.12.1, channel 11) over the on-board antenna. `on_off_light` is a network *former*
  (coordinator-style) — do not use it for Z2M join tests. Switching between examples needs a
  full `esptool erase_flash`, otherwise stale network data makes the end device loop with
  "BDB Device Reboot failed".
- The managed `esp-zigbee-lib` is the newer API (`ezb_*`, headers in `ezbee/`), with `esp_zb_*`
  compat headers. Read the pinned version's headers/examples instead of relying on memory of
  the old `esp_zb_*` API.
- Default console uses UART0 on GPIO16/17 — the same pins as the LD2410C. `sdkconfig.defaults`
  must set the console to USB-Serial-JTAG only and disable the UART console.
- WSL without systemd: after `usbipd attach`, run `sudo modprobe cdc_acm` (module not loaded by
  default) and `sudo chmod 666 /dev/ttyACM0` (no udev; resets on re-enumeration). To read the log
  without a TTY, use pyserial with RTS toggled to reset the board.

## Conventions

- Pin numbers, timings and rate limits live in one header (`app_config.h`), not scattered.
- **Network load matters**: never forward LD2410C data frames (~10 Hz) directly. Report on
  change, add a slow heartbeat, and keep a global report rate limit (plan §4).
- Reports go straight to the coordinator (short 0x0000, EP1), not through the binding table, so Z2M
  gets them even after a rejoin wiped its bindings/reporting. All five endpoints are re-sent ~3 s
  after every join/rejoin and every `APP_HEARTBEAT_S` (600 s). Verified: refresh sent after rejoin.
- LD2410C: 256000 baud 8N1. Config commands must be wrapped in enable-config (0xFF) and
  end-config (0xFE); data frames stop while in config mode. Bluetooth needs a module restart
  (0xA3) to take effect. The module is the source of truth for both switches (no NVS):
  Bluetooth is read with the MAC query (0xA5; MAC 08:05:04:03:02:01 = off), engineering mode
  from the type byte of every data frame. Engineering mode resets to off on module power cycle,
  and the Zigbee switch then follows. Protocol checked against ESPHome's ld2410 component.
- Keep the LD2410 frame parser and PIR debounce logic free of ESP-IDF dependencies so they
  can be unit-tested on the host.
- Don't commit build output (`build/`, `managed_components/`, `sdkconfig` unless it is
  deliberately tracked; prefer `sdkconfig.defaults`). Commit only when asked.

## Verify before relying on

Items in the plan that came from memory and must be checked against hardware or docs:
Verified since: RF-switch polarity, pad-to-GPIO mapping (Seeed wiki), LD2410C command codes and
Bluetooth behaviour (on a real module), Z2M converter API (2.12.1). Still unverified: HC-SR501 sensors
on real hardware.
