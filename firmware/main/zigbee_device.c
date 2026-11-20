/*
 * Zigbee end device: 3 occupancy endpoints + 2 On/Off endpoints.
 * Uses the esp-zigbee-lib 2.x ("ezb_*") API; structure follows the SDK's
 * temperature_sensor example (Public Domain / CC0).
 */
#include "zigbee_device.h"

#include <stdbool.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_zigbee.h"
#include "ezbee/zha.h"

static const char *TAG = "zigbee";

#define ZB_STORAGE_PARTITION "nvs"

#define OCCUPANCY_EP_COUNT 3
static const uint8_t s_occupancy_eps[OCCUPANCY_EP_COUNT] = {APP_EP_PIR1, APP_EP_PIR2, APP_EP_LD2410};
static const uint8_t s_switch_eps[2] = {APP_EP_LD2410_BT, APP_EP_LD2410_ENG};

static zigbee_switch_cb_t s_switch_cb;
static bool s_occupancy[OCCUPANCY_EP_COUNT];
static bool s_switch[2];
/* Set once the stack is operational. Updates made before that are cached and applied then. */
static volatile bool s_ready;
static volatile bool s_joined;
static esp_timer_handle_t s_join_report_timer;
static esp_timer_handle_t s_heartbeat_timer;
static esp_timer_handle_t s_commission_timer;
static ezb_bdb_comm_mode_t s_pending_mode;

/* ------------------------------------------------------------------------ */
/* Commissioning retry helper                                               */
/* ------------------------------------------------------------------------ */

static void commission_timer_cb(void *arg)
{
    (void)arg;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    (void)ezb_bdb_start_top_level_commissioning(s_pending_mode);
    esp_zigbee_lock_release();
}

static void schedule_commissioning(ezb_bdb_comm_mode_t mode, uint32_t delay_ms)
{
    s_pending_mode = mode;
    if (!s_commission_timer) {
        const esp_timer_create_args_t args = {.callback = commission_timer_cb, .name = "zb_commission"};
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_commission_timer));
    }
    esp_timer_stop(s_commission_timer);
    ESP_ERROR_CHECK(esp_timer_start_once(s_commission_timer, (uint64_t)delay_ms * 1000));
}

/* Push every cached value into the ZCL attributes. Called from the stack task when it becomes operational. */
static void apply_all_cached(void)
{
    for (int i = 0; i < OCCUPANCY_EP_COUNT; i++) {
        uint8_t v = s_occupancy[i] ? 1 : 0;
        ezb_zcl_set_attr_value(s_occupancy_eps[i], EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING, EZB_ZCL_CLUSTER_SERVER,
                               EZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID, EZB_ZCL_STD_MANUF_CODE, &v, false);
    }
    for (size_t i = 0; i < sizeof(s_switch_eps); i++) {
        ezb_zcl_set_attr_value(s_switch_eps[i], EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER,
                               EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, EZB_ZCL_STD_MANUF_CODE, &s_switch[i], false);
    }
}

/* ------------------------------------------------------------------------ */
/* Signal + ZCL handlers                                                    */
/* ------------------------------------------------------------------------ */

static void on_joined(void);
static void on_left(void);

static bool app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t type = ezb_app_signal_get_type(app_signal);

    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            s_ready = true;
            apply_all_cached();
            ESP_LOGI(TAG, "Device started up in%s factory-reset mode", ezb_bdb_is_factory_new() ? "" : " non");
            if (ezb_bdb_is_factory_new()) {
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Rejoined existing network");
                on_joined();
            }
        } else {
            ESP_LOGW(TAG, "%s failed with status(0x%02x), retrying", ezb_app_signal_to_string(type), status);
            schedule_commissioning(EZB_BDB_MODE_INITIALIZATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ezb_extpanid_t ext;
            ezb_nwk_get_extended_panid(&ext);
            on_joined();
            ESP_LOGI(TAG, "Joined network: PAN 0x%04hx, channel %d, short addr 0x%04hx", ezb_nwk_get_panid(),
                     ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
        } else {
            ESP_LOGW(TAG, "Network steering failed with status(0x%02x), retrying", status);
            schedule_commissioning(EZB_BDB_MODE_NETWORK_STEERING, 5000);
        }
    } break;
    case EZB_ZDO_SIGNAL_LEAVE: {
        const ezb_zdo_signal_leave_params_t *p = ezb_app_signal_get_params(app_signal);
        ESP_LOGI(TAG, "Left network (type 0x%02x), will look for a new one", p->leave_type);
        on_left();
        schedule_commissioning(EZB_BDB_MODE_NETWORK_STEERING, 2000);
    } break;
    default:
        ESP_LOGD(TAG, "Zigbee signal: %s", ezb_app_signal_to_string(type));
        break;
    }
    return true;
}

static void zcl_core_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id) {
    case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID: {
        ezb_zcl_set_attr_value_message_t *msg = (ezb_zcl_set_attr_value_message_t *)message;
        msg->out.result = EZB_ZCL_STATUS_SUCCESS;
        if (msg->info.cluster_id == EZB_ZCL_CLUSTER_ID_ON_OFF &&
            msg->in.attribute.id == EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && msg->in.attribute.data.value) {
            bool on = *(bool *)msg->in.attribute.data.value;
            ESP_LOGI(TAG, "EP%u switch set to %s", msg->info.dst_ep, on ? "on" : "off");
            if (s_switch_cb) {
                s_switch_cb(msg->info.dst_ep, on);
            }
        }
    } break;
    case EZB_ZCL_CORE_DEFAULT_RSP_CB_ID:
        break;
    default:
        ESP_LOGD(TAG, "ZCL core action 0x%04lx", callback_id);
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Endpoint construction                                                    */
/* ------------------------------------------------------------------------ */

static ezb_af_ep_desc_t create_endpoint(uint8_t ep_id, uint16_t device_id)
{
    const ezb_af_ep_config_t cfg = {
        .ep_id = ep_id,
        .app_profile_id = EZB_AF_HA_PROFILE_ID,
        .app_device_id = device_id,
        .app_device_version = 0,
    };
    return ezb_af_create_endpoint_desc(&cfg);
}

static void add_basic(ezb_af_ep_desc_t ep)
{
    const ezb_zcl_basic_cluster_server_config_t cfg = {
        .zcl_version = 8,
        .power_source = EZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    ezb_zcl_cluster_desc_t basic = ezb_zcl_basic_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)APP_MANUFACTURER_NAME);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)APP_MODEL_IDENTIFIER);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep, basic));
}

static void add_identify(ezb_af_ep_desc_t ep)
{
    const ezb_zcl_identify_cluster_server_config_t cfg = {.identify_time = 0};
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(
        ep, ezb_zcl_identify_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER)));
}

static void add_occupancy_endpoint(ezb_af_device_desc_t dev, uint8_t ep_id, bool with_basic, uint8_t sensor_type,
                                   uint8_t type_bitmap)
{
    ezb_af_ep_desc_t ep = create_endpoint(ep_id, EZB_ZHA_OCCUPANCY_SENSOR_DEVICE_ID);
    if (with_basic) {
        add_basic(ep);
    }
    add_identify(ep);
    const ezb_zcl_occupancy_sensing_cluster_server_config_t cfg = {
        .occupancy = 0,
        .occupancy_sensor_type = sensor_type,
        .occupancy_sensor_type_bitmap = type_bitmap,
    };
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(
        ep, ezb_zcl_occupancy_sensing_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER)));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev, ep));
}

static void add_switch_endpoint(ezb_af_device_desc_t dev, uint8_t ep_id)
{
    ezb_af_ep_desc_t ep = create_endpoint(ep_id, EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID);
    add_identify(ep);
    const ezb_zcl_on_off_cluster_server_config_t cfg = {.on_off = false};
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep, ezb_zcl_on_off_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER)));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev, ep));
}

static void create_device(void)
{
    ezb_af_device_desc_t dev = ezb_af_create_device_desc();

    add_occupancy_endpoint(dev, APP_EP_PIR1, true, EZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_PIR, 0x01);
    add_occupancy_endpoint(dev, APP_EP_PIR2, false, EZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_PIR, 0x01);
    add_occupancy_endpoint(dev, APP_EP_LD2410, false, EZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_ULTRASONIC, 0x02);
    add_switch_endpoint(dev, APP_EP_LD2410_BT);
    add_switch_endpoint(dev, APP_EP_LD2410_ENG);

    ESP_ERROR_CHECK(ezb_af_device_desc_register(dev));
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);
}

/* ------------------------------------------------------------------------ */
/* Stack task                                                               */
/* ------------------------------------------------------------------------ */

static void zigbee_main_task(void *arg)
{
    (void)arg;
    esp_zigbee_config_t config = {
        .device_config =
            {
                .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,
                .install_code_policy = false,
                .zed_config = {.ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN, .keep_alive = 4000},
            },
        .platform_config =
            {
                .storage_partition_name = ZB_STORAGE_PARTITION,
                .radio_config = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE},
            },
    };

    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(APP_ZB_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(APP_ZB_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(app_signal_handler));

    create_device();

    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

esp_err_t zigbee_device_start(zigbee_switch_cb_t switch_cb)
{
    s_switch_cb = switch_cb;
    return xTaskCreate(zigbee_main_task, "zigbee_main", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

/* ------------------------------------------------------------------------ */
/* Public update API                                                        */
/* ------------------------------------------------------------------------ */

/* Report one attribute straight to the coordinator (short 0x0000, endpoint 1) instead of through the
 * binding table, so Z2M receives it even when its bindings/reporting setup were lost (e.g. after a rejoin).
 * Caller must hold the Zigbee lock (or run in the stack task). */
static void report_attr(uint8_t ep, uint16_t cluster_id, uint16_t attr_id)
{
    ezb_zcl_report_attr_cmd_t cmd = {
        .cmd_ctrl =
            {
                .fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI,
                .fc.dis_default_rsp = 1,
                .dst_addr = EZB_ADDRESS_SHORT(0x0000),
                .dst_ep = 1,
                .src_ep = ep,
                .cluster_id = cluster_id,
            },
        .payload = {.attr_id = attr_id},
    };
    ezb_err_t err = ezb_zcl_report_attr_cmd_req(&cmd);
    if (err != EZB_ERR_NONE) {
        ESP_LOGD(TAG, "Report EP%u cluster 0x%04x failed: 0x%04x", ep, cluster_id, err);
    }
}

/* Report every endpoint's current value. Caller must hold the Zigbee lock. */
static void report_all_locked(void)
{
    for (int i = 0; i < OCCUPANCY_EP_COUNT; i++) {
        report_attr(s_occupancy_eps[i], EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
                    EZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID);
    }
    for (size_t i = 0; i < sizeof(s_switch_eps); i++) {
        report_attr(s_switch_eps[i], EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
    }
}

static void refresh_timer_cb(void *arg)
{
    (void)arg;
    if (!s_ready || !s_joined) {
        return;
    }
    esp_zigbee_lock_acquire(portMAX_DELAY);
    report_all_locked();
    esp_zigbee_lock_release();
    ESP_LOGI(TAG, "State refresh sent to coordinator");
}

static void on_joined(void)
{
    s_joined = true;
    if (!s_heartbeat_timer) {
        const esp_timer_create_args_t hb = {.callback = refresh_timer_cb, .name = "zb_heartbeat"};
        const esp_timer_create_args_t jr = {.callback = refresh_timer_cb, .name = "zb_join_report"};
        ESP_ERROR_CHECK(esp_timer_create(&hb, &s_heartbeat_timer));
        ESP_ERROR_CHECK(esp_timer_create(&jr, &s_join_report_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_heartbeat_timer, (uint64_t)APP_HEARTBEAT_S * 1000000ULL));
    }
    /* Shortly after joining, tell the coordinator the current state (restart the delay on repeat joins). */
    esp_timer_stop(s_join_report_timer);
    ESP_ERROR_CHECK(esp_timer_start_once(s_join_report_timer, (uint64_t)APP_JOIN_REPORT_DELAY_MS * 1000ULL));
}

static void on_left(void)
{
    s_joined = false;
}

void zigbee_device_set_occupancy(uint8_t ep, bool occupied)
{
    for (int i = 0; i < OCCUPANCY_EP_COUNT; i++) {
        if (s_occupancy_eps[i] != ep) {
            continue;
        }
        if (s_occupancy[i] == occupied) {
            return; /* unchanged: do not spend airtime */
        }
        s_occupancy[i] = occupied;
        if (!s_ready) {
            return; /* applied by apply_all_cached() once the stack is up */
        }
        uint8_t value = occupied ? 1 : 0;
        esp_zigbee_lock_acquire(portMAX_DELAY);
        ezb_zcl_set_attr_value(ep, EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING, EZB_ZCL_CLUSTER_SERVER,
                               EZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID, EZB_ZCL_STD_MANUF_CODE, &value, false);
        report_attr(ep, EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING, EZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID);
        esp_zigbee_lock_release();
        ESP_LOGI(TAG, "EP%u occupancy -> %d", ep, occupied);
        return;
    }
}

void zigbee_device_set_switch(uint8_t ep, bool on)
{
    for (size_t i = 0; i < sizeof(s_switch_eps); i++) {
        if (s_switch_eps[i] != ep) {
            continue;
        }
        s_switch[i] = on;
        if (!s_ready) {
            return;
        }
        esp_zigbee_lock_acquire(portMAX_DELAY);
        ezb_zcl_set_attr_value(ep, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                               EZB_ZCL_STD_MANUF_CODE, &on, false);
        report_attr(ep, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
        esp_zigbee_lock_release();
        return;
    }
}

void zigbee_device_factory_reset(void)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_bdb_reset_via_local_action();
    esp_zigbee_lock_release();
}
