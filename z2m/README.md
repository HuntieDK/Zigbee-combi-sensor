# Zigbee2MQTT converter

`combi-sensor.mjs` teaches Z2M about the five endpoints of the combi-sensor.

## Install (HA add-on)

1. Copy the file to the Z2M data directory under `external_converters/`
   (Home Assistant add-on: `/config/zigbee2mqtt/external_converters/combi-sensor.mjs`,
   e.g. via the Samba or File editor add-on).
2. Restart Zigbee2MQTT.
3. The device (manufacturer "DIY", model "combi-sensor") should now be supported.
   If it was paired before the converter existed, use **Reconfigure** on the device
   page so reporting and bindings are set up; re-pairing is not required.

## Expected entities

| Z2M property | Endpoint | Meaning |
|---|---|---|
| `occupancy_pir1` | 1 | PIR 1 present |
| `occupancy_pir2` | 2 | PIR 2 present |
| `occupancy_radar` | 3 | LD2410C presence |
| `state_radar_bluetooth` | 4 | LD2410C Bluetooth switch |
| `state_radar_engineering` | 5 | LD2410C engineering-mode switch |

Later (phase 2): still / moving energy as analog sensors.

The converter API changes between Z2M releases; re-check after upgrading Z2M.
