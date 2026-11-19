#include "ld2410_proto.h"

#include <string.h>

static const uint8_t CMD_HEADER[4]  = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FOOTER[4]  = {0x04, 0x03, 0x02, 0x01};
static const uint8_t DATA_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t DATA_FOOTER[4] = {0xF8, 0xF7, 0xF6, 0xF5};

void ld2410_parser_init(ld2410_parser_t *p)
{
    p->pos = 0;
}

static bool decode_data(const uint8_t *f, size_t total, ld2410_event_t *ev)
{
    /* header(4) len(2) type(1) 0xAA target(1) mdist(2) men(1) sdist(2) sen(1) ddist(2) 0x55 0x00 footer(4) */
    if (total < 23 || f[7] != 0xAA || f[total - 6] != 0x55 || f[total - 5] != 0x00) {
        return false;
    }
    if (f[8] >= 4) {
        return false; /* calibration states, not a presence report */
    }
    ev->type = LD2410_EV_DATA;
    ev->engineering = (f[6] == 0x01);
    ev->target_state = f[8];
    ev->moving_dist_cm = (uint16_t)(f[9] | (f[10] << 8));
    ev->moving_energy = f[11];
    ev->still_dist_cm = (uint16_t)(f[12] | (f[13] << 8));
    ev->still_energy = f[14];
    ev->detect_dist_cm = (uint16_t)(f[15] | (f[16] << 8));
    return true;
}

static bool decode_ack(const uint8_t *f, size_t total, ld2410_event_t *ev)
{
    /* header(4) len(2) cmd(1) 0x01 status(2) payload... footer(4) */
    if (total < 14 || f[7] != 0x01) {
        return false;
    }
    ev->type = LD2410_EV_ACK;
    ev->cmd = f[6];
    ev->ok = (f[8] == 0 && f[9] == 0);
    size_t plen = total - 14;
    if (plen > sizeof(ev->payload)) {
        plen = sizeof(ev->payload);
    }
    memcpy(ev->payload, &f[10], plen);
    ev->payload_len = (uint8_t)plen;
    return true;
}

bool ld2410_parser_feed(ld2410_parser_t *p, uint8_t byte, ld2410_event_t *ev)
{
    /* While reading the 4-byte header, accept either header; resync on mismatch. */
    if (p->pos < 4) {
        bool cmd_ok  = (byte == CMD_HEADER[p->pos]) && (p->pos == 0 || memcmp(p->buf, CMD_HEADER, p->pos) == 0);
        bool data_ok = (byte == DATA_HEADER[p->pos]) && (p->pos == 0 || memcmp(p->buf, DATA_HEADER, p->pos) == 0);
        if (cmd_ok || data_ok) {
            p->buf[p->pos++] = byte;
        } else {
            /* mismatch: this byte may itself start a new header */
            p->pos = 0;
            if (byte == CMD_HEADER[0] || byte == DATA_HEADER[0]) {
                p->buf[p->pos++] = byte;
            }
        }
        return false;
    }

    p->buf[p->pos++] = byte;

    if (p->pos == 6) {
        size_t len = (size_t)(p->buf[4] | (p->buf[5] << 8));
        if (len == 0 || len + 10 > LD2410_MAX_FRAME) {
            p->pos = 0; /* implausible length: resync */
        }
        return false;
    }
    if (p->pos < 6) {
        return false;
    }

    size_t total = (size_t)(p->buf[4] | (p->buf[5] << 8)) + 10;
    if (p->pos < total) {
        return false;
    }

    /* Complete frame */
    bool is_data = (memcmp(p->buf, DATA_HEADER, 4) == 0);
    const uint8_t *footer = is_data ? DATA_FOOTER : CMD_FOOTER;
    bool ok = false;
    if (memcmp(&p->buf[total - 4], footer, 4) == 0) {
        memset(ev, 0, sizeof(*ev));
        ok = is_data ? decode_data(p->buf, total, ev) : decode_ack(p->buf, total, ev);
    }
    p->pos = 0;
    return ok;
}

size_t ld2410_build_command(uint8_t cmd, const uint8_t *value, size_t value_len, uint8_t *out, size_t out_size)
{
    size_t total = 4 + 2 + 2 + value_len + 4;
    if (out_size < total) {
        return 0;
    }
    size_t len = 2 + value_len;
    memcpy(out, CMD_HEADER, 4);
    out[4] = (uint8_t)(len & 0xFF);
    out[5] = (uint8_t)(len >> 8);
    out[6] = cmd;
    out[7] = 0x00;
    if (value_len) {
        memcpy(&out[8], value, value_len);
    }
    memcpy(&out[8 + value_len], CMD_FOOTER, 4);
    return total;
}

bool ld2410_bluetooth_enabled_from_mac(const uint8_t mac[6])
{
    static const uint8_t NO_MAC[6] = {0x08, 0x05, 0x04, 0x03, 0x02, 0x01};
    return memcmp(mac, NO_MAC, 6) != 0;
}
