/* Host-side unit tests for the LD2410C protocol and presence filter. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../main/ld2410_proto.h"
#include "../../main/presence_filter.h"

static bool feed_all(ld2410_parser_t *p, const uint8_t *d, size_t n, ld2410_event_t *ev)
{
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        if (ld2410_parser_feed(p, d[i], ev)) {
            got = true;
        }
    }
    return got;
}

static void test_build(void)
{
    uint8_t out[32];
    const uint8_t v[2] = {0x01, 0x00};
    static const uint8_t enable[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    size_t n = ld2410_build_command(LD2410_CMD_ENABLE_CONFIG, v, 2, out, sizeof(out));
    assert(n == sizeof(enable) && memcmp(out, enable, n) == 0);

    static const uint8_t disable[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};
    n = ld2410_build_command(LD2410_CMD_DISABLE_CONFIG, NULL, 0, out, sizeof(out));
    assert(n == sizeof(disable) && memcmp(out, disable, n) == 0);

    assert(ld2410_build_command(0x62, NULL, 0, out, 5) == 0); /* too small */
}

static void test_data_frame(void)
{
    /* normal-mode frame: type 02, moving target 1, moving 100 cm energy 50, still 120 cm energy 30, detect 100 */
    static const uint8_t f[] = {0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x01, 0x64, 0x00, 0x32, 0x78,
                                0x00, 0x1E, 0x64, 0x00, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};
    ld2410_parser_t p;
    ld2410_event_t ev;
    ld2410_parser_init(&p);
    assert(feed_all(&p, f, sizeof(f), &ev));
    assert(ev.type == LD2410_EV_DATA && !ev.engineering && ev.target_state == 1);
    assert(ev.moving_dist_cm == 100 && ev.moving_energy == 50);
    assert(ev.still_dist_cm == 120 && ev.still_energy == 30 && ev.detect_dist_cm == 100);

    /* engineering flag */
    uint8_t g[sizeof(f)];
    memcpy(g, f, sizeof(f));
    g[6] = 0x01;
    assert(feed_all(&p, g, sizeof(g), &ev) && ev.engineering);

    /* calibration state (>= 4) is not a presence report */
    memcpy(g, f, sizeof(f));
    g[8] = 0x04;
    assert(!feed_all(&p, g, sizeof(g), &ev));
}

static void test_engineering_long_frame(void)
{
    /* 45-byte engineering frame: len 0x23 (35), so total = 35 + 10 */
    uint8_t f[45] = {0xF4, 0xF3, 0xF2, 0xF1, 0x23, 0x00, 0x01, 0xAA, 0x03};
    f[11] = 7;  /* moving energy */
    f[14] = 9;  /* still energy */
    f[39] = 0x55;
    f[40] = 0x00;
    f[41] = 0xF8; f[42] = 0xF7; f[43] = 0xF6; f[44] = 0xF5;
    ld2410_parser_t p;
    ld2410_event_t ev;
    ld2410_parser_init(&p);
    assert(feed_all(&p, f, sizeof(f), &ev));
    assert(ev.engineering && ev.target_state == 3 && ev.moving_energy == 7 && ev.still_energy == 9);
}

static void test_ack(void)
{
    static const uint8_t enable_ack[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x01, 0x00,
                                         0x40, 0x00, 0x04, 0x03, 0x02, 0x01};
    ld2410_parser_t p;
    ld2410_event_t ev;
    ld2410_parser_init(&p);
    assert(feed_all(&p, enable_ack, sizeof(enable_ack), &ev));
    assert(ev.type == LD2410_EV_ACK && ev.cmd == 0xFF && ev.ok && ev.payload_len == 4);

    /* failed status */
    uint8_t bad[sizeof(enable_ack)];
    memcpy(bad, enable_ack, sizeof(bad));
    bad[8] = 0x01;
    assert(feed_all(&p, bad, sizeof(bad), &ev) && !ev.ok);

    /* MAC query ack: Bluetooth on (real MAC) vs off (08:05:04:03:02:01) */
    uint8_t mac_on[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x0A, 0x00, 0xA5, 0x01, 0x00, 0x00,
                        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x04, 0x03, 0x02, 0x01};
    assert(feed_all(&p, mac_on, sizeof(mac_on), &ev) && ev.cmd == 0xA5 && ev.payload_len == 6);
    assert(ld2410_bluetooth_enabled_from_mac(ev.payload));
    uint8_t mac_off[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x0A, 0x00, 0xA5, 0x01, 0x00, 0x00,
                         0x08, 0x05, 0x04, 0x03, 0x02, 0x01, 0x04, 0x03, 0x02, 0x01};
    assert(feed_all(&p, mac_off, sizeof(mac_off), &ev));
    assert(!ld2410_bluetooth_enabled_from_mac(ev.payload));
}

static void test_resync(void)
{
    static const uint8_t ack[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x01, 0x00,
                                  0x40, 0x00, 0x04, 0x03, 0x02, 0x01};
    uint8_t stream[64];
    size_t n = 0;
    /* leading garbage, including a partial header, then a valid frame */
    const uint8_t junk[] = {0x00, 0xFD, 0xFC, 0x12, 0xF4, 0xF3, 0xAB};
    memcpy(stream, junk, sizeof(junk));
    n = sizeof(junk);
    memcpy(&stream[n], ack, sizeof(ack));
    n += sizeof(ack);
    ld2410_parser_t p;
    ld2410_event_t ev;
    ld2410_parser_init(&p);
    assert(feed_all(&p, stream, n, &ev) && ev.cmd == 0xFF);

    /* corrupted footer is dropped, next good frame still parses */
    uint8_t bad[sizeof(ack)];
    memcpy(bad, ack, sizeof(bad));
    bad[sizeof(bad) - 1] = 0x00;
    assert(!feed_all(&p, bad, sizeof(bad), &ev));
    assert(feed_all(&p, ack, sizeof(ack), &ev));

    /* absurd length is rejected without wedging the parser */
    const uint8_t huge[] = {0xFD, 0xFC, 0xFB, 0xFA, 0xFF, 0xFF};
    assert(!feed_all(&p, huge, sizeof(huge), &ev));
    assert(feed_all(&p, ack, sizeof(ack), &ev));
}

static void test_presence_filter(void)
{
    presence_filter_t f;
    presence_filter_init(&f, 1000);
    assert(!presence_filter_update(&f, false, 0));
    assert(presence_filter_update(&f, true, 100) && f.present);   /* on immediately */
    assert(!presence_filter_update(&f, true, 200));               /* no repeat */
    assert(!presence_filter_update(&f, false, 300));              /* absence starts */
    assert(!presence_filter_update(&f, false, 1299));             /* 999 ms */
    assert(presence_filter_update(&f, false, 1300) && !f.present);/* off after 1000 ms */
    /* brief absence is bridged */
    assert(presence_filter_update(&f, true, 2000));
    assert(!presence_filter_update(&f, false, 2100));
    assert(!presence_filter_update(&f, true, 2500));
    assert(!presence_filter_update(&f, false, 2600));
    assert(!presence_filter_update(&f, false, 3500));             /* timer restarted at 2600 */
    assert(presence_filter_update(&f, false, 3600) && !f.present);
}

int main(void)
{
    test_build();
    test_data_frame();
    test_engineering_long_frame();
    test_ack();
    test_resync();
    test_presence_filter();
    puts("ld2410_proto + presence_filter: all tests passed");
    return 0;
}
