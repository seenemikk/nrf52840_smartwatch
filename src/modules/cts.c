#include <zephyr/kernel.h>

#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/services/cts_client.h>
#include <bluetooth/gatt_dm.h>

#include <time.h>
#include <date_time.h>

#define MODULE cts
#include <caf/events/module_state_event.h>
#include <caf/events/ble_common_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_SMARTWATCH_CTS_LOG_LEVEL);

#define INITIAL_TIMESTAMP   1640995200  // 2022/01/01 00:00:00 GMT+0000
#define RESET_MAGIC         0xfeedbabe

#if IS_ENABLED(CONFIG_SMARTWATCH_CTS_BACKUP)

#if CONFIG_SMARTWATCH_CTS_BACKUP_INTERVAL_SECONDS < 1
#error "Invalid backup interval"
#endif // CONFIG_SMARTWATCH_CTS_BACKUP_INTERVAL_SECONDS < 1

__attribute__((section(".noinit"))) static volatile uint32_t backup_magic;
__attribute__((section(".noinit"))) static volatile time_t backup_timestamp;
static struct k_work_delayable backup_work;

#endif // IS_ENABLED(CONFIG_SMARTWATCH_CTS_BACKUP)

// TODO add date_updated/date_update event
// TODO is there a way to hook system reset, so we wouldn't need constant backups?

#if IS_ENABLED(CONFIG_BT_CTS_CLIENT)

static struct bt_cts_client cts_c;

static void cts_cb(struct bt_cts_client *cts_c, struct bt_cts_current_time *current_time)
{
    struct bt_cts_exact_time_256 *time = &current_time->exact_time_256;

    struct tm tm = {
        .tm_sec = time->seconds, .tm_min = time->minutes, .tm_hour = time->hours,
        .tm_mday = time->day, .tm_mon = time->month, .tm_year = time->year - 1900
    };
    int err = date_time_set(&tm);
    if (err) {
        LOG_ERR("Failed setting datetime (%d)", err);
    } else {
        LOG_INF("Time updated");
    }
}

static void discovery_completed_cb(struct bt_gatt_dm *dm, void *ctx)
{
    LOG_INF("CTS service found");

    int err = bt_cts_handles_assign(dm, &cts_c);
    if (err) {
        LOG_ERR("Could not assign CTS client handles (%d)", err);
        goto end;
    }

    err = bt_cts_subscribe_current_time(&cts_c, cts_cb);
    if (err) {
        LOG_ERR("Failed subscribing to CTS service (%d)", err);
        goto end;
    }

end:
    err = bt_gatt_dm_data_release(dm);
    if (err) {
        LOG_ERR("Failed releasing discovery data (%d)", err);
    }
}

static void discovery_service_not_found_cb(struct bt_conn *conn, void *ctx)
{
    LOG_WRN("CTS service not found");
}

static void discovery_error_found_cb(struct bt_conn *conn, int err, void *ctx)
{
    LOG_ERR("Discovery error (%d)", err);
}

static const struct bt_gatt_dm_cb discovery_cb = {
    .completed = discovery_completed_cb,
    .service_not_found = discovery_service_not_found_cb,
    .error_found = discovery_error_found_cb,
};

static void discovery_start(struct bt_conn *conn)
{
    int err = bt_gatt_dm_start(conn, BT_UUID_CTS, &discovery_cb, NULL);
    if (err) {
        LOG_ERR("Failed to start CTS discovery (%d)", err);
    }
}

#endif // IS_ENABLED(CONFIG_BT_CTS_CLIENT)

#if IS_ENABLED(CONFIG_SMARTWATCH_CTS_BACKUP)

static void backup_timestamp_fn(struct k_work *work)
{
    int64_t timestamp_ms = 0;
    if (date_time_now(&timestamp_ms) == 0) {
        backup_timestamp = timestamp_ms / 1000;
    }

    k_work_reschedule(&backup_work, K_SECONDS(CONFIG_SMARTWATCH_CTS_BACKUP_INTERVAL_SECONDS));
}

#endif // IS_ENABLED(CONFIG_SMARTWATCH_CTS_BACKUP)

static void cts_init(void)
{
    static bool initialized;

    __ASSERT_NO_MSG(!initialized);
    initialized = true;

    int err = 0;
    time_t timestamp = INITIAL_TIMESTAMP;

#if IS_ENABLED(CONFIG_SMARTWATCH_CTS_BACKUP)
    if (backup_magic != RESET_MAGIC) {
        LOG_WRN("Unable to restore previous datetime");
        backup_magic = RESET_MAGIC;
        backup_timestamp = INITIAL_TIMESTAMP;
    }
    timestamp = backup_timestamp;

    k_work_init_delayable(&backup_work, backup_timestamp_fn);
    k_work_reschedule(&backup_work, K_SECONDS(CONFIG_SMARTWATCH_CTS_BACKUP_INTERVAL_SECONDS));
#endif // IS_ENABLED(CONFIG_SMARTWATCH_CTS_BACKUP)

    struct tm *tm = gmtime(&timestamp);
    if (tm == NULL || (err = date_time_set(tm))) {
        LOG_ERR("Failed setting initial datetime (%d)", err);
        module_set_state(MODULE_STATE_ERROR);
        return;
    }

#if IS_ENABLED(CONFIG_BT_CTS_CLIENT)
    err = bt_cts_client_init(&cts_c);
    if (err) {
        LOG_ERR("Failed initializing CTS client (%d)", err);
        module_set_state(MODULE_STATE_ERROR);
        return;
    }
#endif // IS_ENABLED(CONFIG_BT_CTS_CLIENT)

    LOG_INF("CTS module initialized");
    module_set_state(MODULE_STATE_READY);
}

static bool app_event_handler(const struct app_event_header *aeh)
{
    if (is_module_state_event(aeh)) {
        struct module_state_event *event = cast_module_state_event(aeh);

        if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) cts_init();

        return false;
    }

#if IS_ENABLED(CONFIG_BT_CTS_CLIENT)
    if (is_ble_peer_event(aeh)) {
        struct ble_peer_event *event = cast_ble_peer_event(aeh);

        if (event->state == PEER_STATE_SECURED) discovery_start(event->id);

        return false;
    }
#endif // IS_ENABLED(CONFIG_BT_CTS_CLIENT)

    /* If event is unhandled, unsubscribe. */
    __ASSERT_NO_MSG(false);

    return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);

#if IS_ENABLED(CONFIG_BT_CTS_CLIENT)
APP_EVENT_SUBSCRIBE(MODULE, ble_peer_event);
#endif // IS_ENABLED(CONFIG_BT_CTS_CLIENT)
