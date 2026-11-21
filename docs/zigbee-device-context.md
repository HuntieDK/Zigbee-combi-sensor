# Context: building a Zigbee device on the XIAO ESP32-C6

Reusable findings from the combi-sensor project (ESP-IDF + `esp-zigbee-lib` + Zigbee2MQTT + Home
Assistant). Written so a future session, human or Claude, can start a new Zigbee device without
repeating the discoveries. Items marked **(unverified)** came from memory or docs and were not tested
on hardware.

Versions used: ESP-IDF v5.5.6, `espressif/esp-zigbee-lib` 2.1.0, Zigbee2MQTT 2.12.1 (bundling
zigbee-herdsman-converters 26.76.0), Sonoff ZBDongle-P (CC2652P) coordinator, Ubuntu 26.04 on WSL2
with Python 3.14.

## 1. Toolchain setup (WSL2 / Ubuntu)

- apt: `git wget flex bison gperf python3-venv python3-pip cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0`
- Clone ESP-IDF at the version the SDK README recommends (check
  `esp-zigbee-sdk` README "Supported ESP-IDF versions"), `--recursive --shallow-submodules`.
  `./install.sh esp32c6` worked with Python 3.14 on IDF 5.5.6.
- Activate every shell: `export IDF_TOOLS_PATH=$HOME/.espressif; . ~/esp/esp-idf/export.sh`.
- Run everything as the normal user. A root session leaves root-owned files in `~/.espressif`,
  `~/esp` and `/tmp/esp_idf_activate_<user>`, which breaks `export.sh` with a PermissionError.
  Fix with a one-off `sudo rm -rf /tmp/esp_idf_activate_<user>` (or point `TMPDIR` somewhere you own).
- Git clones of GitHub repos can be slow but work. Keep scratch clones out of the project repo.

## 2. Flashing from WSL2

- Windows: `usbipd-win` (tested 5.3.0). Admin PowerShell, once: `usbipd bind --busid <id>`.
  Then `usbipd attach --wsl --busid <id> --auto-attach`. The XIAO appears as `303a:1001`
  ("USB JTAG/serial debug unit").
- Ctrl+C did not reliably stop `--auto-attach` here; closing the PowerShell window does. Stopping it does not
  detach the board. Do not kill
  `usbipd.exe` by name: the Windows service has the same name.
- WSL without systemd: `/dev/ttyACM0` does not appear until `sudo modprobe cdc_acm` (module present,
  not loaded). Permissions are root-only without udev, so `sudo chmod 666 /dev/ttyACM0`; this resets
  whenever the board re-enumerates.
- The C6 re-enumerates on reset, so the port can vanish mid-read. Without `--auto-attach` it must be
  re-attached each time. Reading logs without a TTY: pyserial, toggle RTS to reset
  (`s.dtr=False; s.rts=True; sleep(0.1); s.rts=False`), and reopen the port in a retry loop.
- Boot mode: holding BOOT while resetting or powering up enters the ROM bootloader (useful if the
  device is unrecognised or flashing fails).

## 3. XIAO ESP32-C6 hardware facts (verified against the Seeed wiki)

| Pad | GPIO | Note |
|---|---|---|
| D0 / D1 / D2 | 0 / 1 / 2 | analog-capable |
| D3 | 21 | |
| D4 / D5 | 22 / 23 | I2C SDA / SCL |
| D6 / D7 | 16 / 17 | UART TX / RX; also the **default console UART0 pins** |
| D8 / D9 / D10 | 19 / 20 / 18 | SPI SCK / MISO / MOSI |
| BOOT button | 9 | active low, usable as input while running |
| User LED | 15 | active low (verified on the board) |
| RF switch power | 3 | drive LOW to enable the switch |
| Antenna select | 14 | LOW = on-board ceramic (default), HIGH = external U.FL |

- Set GPIO3 LOW, wait ~100 ms, then GPIO14, before the radio starts. Attach the external antenna before
  powering up.
- 4 MB flash. Console: set `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` and disable the UART console, or
  GPIO16/17 are taken by logging.
- 5V pad powers the HLK-LD2410C and HC-SR501 sensors. Their logic outputs are 3.3 V.

## 4. esp-zigbee-lib 2.x API (it is NOT the old `esp_zb_*` API)

- The managed component uses `ezb_*` functions with headers in `ezbee/` (`esp_zigbee.h`, `ezbee/zha.h`).
  `esp_zb_*` compat headers exist. Read the pinned version's headers and its SDK examples instead of relying
  on memory. The SDK examples (`temperature_sensor` for an end device) are the best template.
- `idf_component.yml`: `espressif/esp-zigbee-lib` version `~2.1.0`, `idf >=5.5`.
- `sdkconfig.defaults`: `CONFIG_ZB_ENABLED=y`, `CONFIG_ZB_ZED=y`, custom partition table with
  `nvs` and a 1 K `zb_fct` partition (type `data`, subtype `fat`); the Zigbee stack stores network data
  in `nvs`.
- Startup sequence (own FreeRTOS task, 4 K stack): `esp_zigbee_init(&cfg)` with an end-device config
  (`EZB_NWK_DEVICE_TYPE_END_DEVICE`, `ed_timeout 64MIN`, `keep_alive 4000`, native radio), then
  `ezb_bdb_set_primary_channel_set` / `secondary`, `ezb_app_signal_add_handler`, build endpoints,
  `ezb_af_device_desc_register`, `esp_zigbee_start(false)`, `esp_zigbee_launch_mainloop()`.
- Signal handler pattern: `EZB_ZDO_SIGNAL_SKIP_STARTUP` -> start `EZB_BDB_MODE_INITIALIZATION`.
  On `EZB_BDB_SIGNAL_DEVICE_FIRST_START` / `DEVICE_REBOOT` success: if `ezb_bdb_is_factory_new()` start
  `EZB_BDB_MODE_NETWORK_STEERING`, else it rejoined. On steering failure retry after a few seconds
  (use `esp_timer`, take `esp_zigbee_lock_acquire` inside the callback). On `EZB_ZDO_SIGNAL_LEAVE`
  start steering again.
- No ZHA helper exists for an occupancy sensor or On/Off output, so build endpoints from raw clusters:
  `ezb_af_create_endpoint_desc` with `EZB_AF_HA_PROFILE_ID` and device ID
  (`EZB_ZHA_OCCUPANCY_SENSOR_DEVICE_ID` 0x0107, `EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID` 0x0002), then
  `ezb_af_endpoint_add_cluster_desc(ep, ezb_zcl_<name>_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER))`
  for basic, identify, occupancy sensing, on/off. Basic needs only one endpoint (EP1). Add
  manufacturer and model strings with `ezb_zcl_basic_cluster_desc_add_attr`; Zigbee strings are
  **length-prefixed** (`"\x0c" "combi-sensor"`).
- Update attributes from other tasks: `esp_zigbee_lock_acquire(portMAX_DELAY)` around
  `ezb_zcl_set_attr_value(ep, cluster, EZB_ZCL_CLUSTER_SERVER, attr, EZB_ZCL_STD_MANUF_CODE, &value, false)`.
  Whether setting values before the stack is operational is safe was not tested; the project avoids it by
  caching values and applying them from the signal handler on first start (`apply_all_cached` in `firmware/main/zigbee_device.c`).
- Coordinator writes (e.g. On/Off switch) arrive in the callback registered with
  `ezb_zcl_core_action_handler_register`, id `EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID`
  (`ezb_zcl_set_attr_value_message_t`); set `msg->out.result = EZB_ZCL_STATUS_SUCCESS`.
- Leave network and wipe pairing: `ezb_bdb_reset_via_local_action()`. A leave signal with type 0 follows.
  **It only works while the device is on a network.** After moving a device to a new place its stored
  network is unreachable (log: "BDB Device Reboot failed with status(0x03)" every ~12 s, steering never
  starts) and the call silently does nothing. The pairing is stored in the `nvs` partition, so for that case
  erase it (`esp_partition_erase_range` on `nvs`) and `esp_restart()`; the device then comes up factory-new
  and joins any open network (verified: erasing only that region let it join a different Z2M instantly).

## 5. Zigbee behaviour lessons

- **Reports.** Send them directly to the coordinator (`ezb_zcl_report_attr_cmd_req` with
  `EZB_ADDRESS_SHORT(0x0000)`, `dst_ep = 1`, `fc.dis_default_rsp = 1`). Reports through the binding table
  (`EZB_ADDR_MODE_NONE`) silently go nowhere after a rejoin, because leaving the network wipes Z2M's
  bindings and reporting setup. Result: Z2M shows N/A until someone presses Reconfigure.
- **Fix used:** re-send every endpoint's current value ~3 s after each join or rejoin, plus a slow heartbeat
  (10 min). Z2M then has correct state with no manual step. Report only on change otherwise.
- **Switching example firmware.** The SDK `on_off_light` example *forms* a network (coordinator style) and never
  joins Z2M. Use an end-device example such as `temperature_sensor` for join tests. After flashing a
  different role, run a full `esptool erase_flash`; stale network data otherwise makes an end device loop on
  "BDB Device Reboot failed with status(0x03)".
- **Status 0x03 while steering** just means "no open network found": enable permit-join in Z2M.
- **Long offline gaps.** After the board was off for hours it took ~27 s to rejoin and once dropped off
  the network entirely ("Left network (type 0x00)") and had to re-pair. Keep the steering retry on leave.
- Joining and rejoin signals are handled in the stack task; calling `ezb_*` functions there needs no lock,
  other tasks and timer callbacks do.

## 6. Zigbee2MQTT 2.x external converter

- Z2M 2.x loads converters as ES modules (`.mjs`) from `<data dir>/external_converters/`
  (HA add-on: `/config/zigbee2mqtt/external_converters/`). Files placed in the data dir root are ignored.
  Restart Z2M after copying. A `zigbeeModel` that equals the Basic model ID makes it match.
- Multi-endpoint device: `m.deviceEndpoints({endpoints: {name: id, ...}})` plus modern extends
  (`m.occupancy`, `m.onOff`) with `endpointNames`. Entities become `occupancy_<name>`,
  `state_<name>`.
- **Library bug (zhc 26.76.0, Z2M 2.12.1):** `m.occupancy({endpointNames: [a, b, c]})` renames one shared
  expose object three times -> one bogus `occupancy_a_b_c` entity and three identical labels. Workaround:
  call `m.occupancy` once per endpoint. Check `package.json` of the Z2M release for the bundled
  `zigbee-herdsman-converters` version and read that version's source, not master.
- Changing the converter: copy the file, restart Z2M, **Reconfigure** the device. Deleting the device and
  pairing it again clears Z2M's cached state; stale keys such as `occupancy_1` and `state_4` (left from
  the unsupported period) vanished after that.
- Harmless log noise: after a reconnect Z2M reads `genOnOff` on EP1 and logs `UNSUPPORTED_CLUSTER`.
- LQI is a poor tool for judging antennas: readings for the same board and antenna ranged 65-196 depending on
  placement and time.

## 7. HLK-LD2410C (UART 256000 8N1)

- Frames: command `FD FC FB FA | len16 | cmd16 | value | 04 03 02 01`; data `F4 F3 F2 F1 | len16 | type
  AA | target state | moving dist16 | moving energy | still dist16 | still energy | detect dist16 | 55 00 |
  F8 F7 F6 F5`. Acks carry cmd, `0x01`, a 16-bit status (0 = ok) and payload. Protocol cross-checked
  against ESPHome's `ld2410` component.
- Commands: `FF` enable config (value `01 00`), `FE` end config, `62`/`63` engineering mode on/off,
  `A4` Bluetooth (value `01 00` / `00 00`), `A3` restart, `A5` query MAC (value `01 00`).
- Data frames stop while in config mode, so end every command sequence with `FE`.
- State is readable: Bluetooth from the `A5` MAC (08:05:04:03:02:01 = off), engineering mode from the type
  byte (0x01 engineering, 0x02 normal). Bluetooth needs `A3` restart (~2 s) to take effect, and the restart
  also resets engineering mode to off. No NVS needed.
- Default Bluetooth password is believed to be `HiLink` **(unverified)**.
- Presence = target state != 0. Hold "present" ~1 s after the radar sees nobody, and report no presence if no
  frames arrive for 10 s (unplugged radar).

## 8. Project patterns worth reusing

- All pins, timings and rate limits in one `app_config.h`.
- Parsers, debouncers and filters as pure C (no ESP-IDF includes) with host unit tests built by a small
  `run.sh` (gcc, asserts). They caught wrap-around and resync cases before any hardware test.
- Event-driven reporting plus heartbeat; never forward high-rate sensor frames.
- Long-press BOOT (GPIO9, 5 s, LED warning after 2 s) = pairing reset.
- Re-verify anything copied from memory against the vendor wiki or reference source before relying on it.

## 9. Checklist for the next device

1. Pick an SDK end-device example as template, build it for `esp32c6`, join Z2M with it first.
2. Copy sdkconfig defaults, partition table, signal handler and cache-then-apply pattern from `firmware/`.
3. Define endpoints and clusters; choose model ID and manufacturer strings (length-prefixed).
4. Write the Z2M converter (ES module); check the bundled zhc version for modern-extend bugs.
5. Send direct reports on change, after each join, and on a heartbeat.
6. Add a pairing-reset button.
7. Test join, rejoin after long off, delete and re-pair, and note what needed a Reconfigure.
