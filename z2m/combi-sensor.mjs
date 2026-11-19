// Zigbee2MQTT external converter for the combi-sensor (XIAO ESP32-C6).
// Written against Z2M 2.12.1 (zigbee-herdsman-converters modernExtend API).
//
// Endpoints (see firmware/main/app_config.h):
//   1 pir1             occupancy (PIR 1)
//   2 pir2             occupancy (PIR 2)
//   3 radar            occupancy (HLK-LD2410C presence)
//   4 radar_bluetooth  switch    (LD2410C Bluetooth on/off)
//   5 radar_engineering switch   (LD2410C engineering mode on/off)

import * as m from "zigbee-herdsman-converters/lib/modernExtend";

export default {
    zigbeeModel: ["combi-sensor"],
    model: "combi-sensor",
    vendor: "DIY",
    description: "XIAO ESP32-C6 combi-sensor: 2x PIR + HLK-LD2410C radar",
    extend: [
        m.deviceEndpoints({
            endpoints: {pir1: 1, pir2: 2, radar: 3, radar_bluetooth: 4, radar_engineering: 5},
        }),
        // Reports on change, plus a 1 h heartbeat (library defaults) to keep network traffic low.
        m.occupancy({endpointNames: ["pir1", "pir2", "radar"]}),
        m.onOff({
            endpointNames: ["radar_bluetooth", "radar_engineering"],
            powerOnBehavior: false,
        }),
    ],
    meta: {multiEndpoint: true},
};
