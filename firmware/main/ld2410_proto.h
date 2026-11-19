#pragma once
/* HLK-LD2410C serial protocol: frame builder and streaming parser.
 * Pure C with no ESP-IDF dependencies so it is unit-tested on the host.
 * Frame layouts verified against the ESPHome ld2410 component. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LD2410_MAX_FRAME 64

/* Command words */
#define LD2410_CMD_ENABLE_CONFIG  0xFF
#define LD2410_CMD_DISABLE_CONFIG 0xFE
#define LD2410_CMD_ENABLE_ENG     0x62
#define LD2410_CMD_DISABLE_ENG    0x63
#define LD2410_CMD_RESTART        0xA3
#define LD2410_CMD_BLUETOOTH      0xA4
#define LD2410_CMD_QUERY_MAC      0xA5

typedef enum {
    LD2410_EV_DATA,   /* periodic target report */
    LD2410_EV_ACK,    /* answer to a command */
} ld2410_event_type_t;

typedef struct {
    ld2410_event_type_t type;
    /* LD2410_EV_DATA */
    bool     engineering;      /* module is in engineering mode */
    uint8_t  target_state;     /* 0 none, 1 moving, 2 still, 3 both */
    uint16_t moving_dist_cm;
    uint8_t  moving_energy;
    uint16_t still_dist_cm;
    uint8_t  still_energy;
    uint16_t detect_dist_cm;
    /* LD2410_EV_ACK */
    uint8_t  cmd;              /* command being acknowledged */
    bool     ok;               /* status field == 0 */
    uint8_t  payload[16];      /* bytes after the status field */
    uint8_t  payload_len;
} ld2410_event_t;

typedef struct {
    uint8_t buf[LD2410_MAX_FRAME];
    size_t  pos;
} ld2410_parser_t;

void ld2410_parser_init(ld2410_parser_t *p);

/* Feed one received byte. Returns true when a complete valid frame has been decoded
 * into *ev. Garbage and corrupt frames are skipped (the parser resynchronises). */
bool ld2410_parser_feed(ld2410_parser_t *p, uint8_t byte, ld2410_event_t *ev);

/* Build a command frame into out. Returns the frame length, or 0 if out is too small. */
size_t ld2410_build_command(uint8_t cmd, const uint8_t *value, size_t value_len, uint8_t *out, size_t out_size);

/* Interpret the 6-byte payload of a QUERY_MAC ack: the module reports 08:05:04:03:02:01
 * when Bluetooth is disabled. */
bool ld2410_bluetooth_enabled_from_mac(const uint8_t mac[6]);
