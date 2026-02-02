/**
 * @file native_api.c
 * @brief HardwareOS Native API Server Module Implementation (M05)
 *
 * ESPHome Native API-compatible server for Home Assistant integration.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md
 *
 * Note: This is a framework implementation. Full protocol requires
 * ESPHome protobuf definitions and Noise protocol library.
 */

#include "native_api.h"
#include "timebase.h"
#include <string.h>

#ifndef TEST_HOST
#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

static const char *TAG = "native_api";

/* ============================================================================
 * Hash Function for Entity Keys
 * ============================================================================ */

static uint32_t fnv1a_hash(const char *str)
{
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

/* ============================================================================
 * Module State
 * ============================================================================ */

static struct {
    bool initialized;
    bool running;
    native_api_config_t config;
    native_api_device_info_t device_info;

    /* Entity registry */
    entity_def_t entities[NATIVE_API_MAX_ENTITIES];
    uint8_t entity_count;

    /* Entity state cache */
    struct {
        uint32_t key;
        union {
            bool binary;
            float value;
        } state;
        uint32_t last_publish_ms;
    } state_cache[NATIVE_API_MAX_ENTITIES];

    /* Connection state */
    conn_state_t conn_state;
    int client_socket;
#ifndef TEST_HOST
    TaskHandle_t server_task;
#endif

    /* Callbacks */
    native_api_conn_callback_t conn_callback;
    void *callback_user_data;

    /* Statistics */
    native_api_stats_t stats;
    uint32_t server_start_ms;

    /* Zone entity key mapping */
    struct {
        char zone_id[16];
        uint32_t occupancy_key;
        uint32_t count_key;
    } zone_keys[16];
    uint8_t zone_count;
} s_api = {0};

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

static entity_def_t *find_entity(uint32_t key)
{
    for (int i = 0; i < s_api.entity_count; i++) {
        if (s_api.entities[i].key == key && s_api.entities[i].enabled) {
            return &s_api.entities[i];
        }
    }
    return NULL;
}

static int find_state_cache_idx(uint32_t key)
{
    for (int i = 0; i < s_api.entity_count; i++) {
        if (s_api.state_cache[i].key == key) {
            return i;
        }
    }
    return -1;
}

static void set_conn_state(conn_state_t state)
{
    if (s_api.conn_state != state) {
        s_api.conn_state = state;
        if (s_api.conn_callback) {
            s_api.conn_callback(state, s_api.callback_user_data);
        }
    }
}

/* ============================================================================
 * mDNS
 * ============================================================================ */

void native_api_get_mdns_instance(char *out)
{
#ifndef TEST_HOST
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, 16, "rs1-%02x%02x%02x", mac[3], mac[4], mac[5]);
#else
    strncpy(out, "rs1-test", 16);
#endif
}

#ifndef TEST_HOST
static esp_err_t mdns_start(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Set hostname */
    char instance[16];
    native_api_get_mdns_instance(instance);
    mdns_hostname_set(instance);

    /* Set instance name */
    mdns_instance_name_set(s_api.device_info.friendly_name);

    /* Register ESPHome service */
    mdns_txt_item_t txt[] = {
        {"version", "1.9"},
        {"mac", s_api.device_info.mac_address},
        {"project_name", s_api.device_info.project_name},
        {"project_version", s_api.device_info.project_version},
    };

    err = mdns_service_add(instance, "_esphomelib", "_tcp",
                           s_api.config.port, txt, 4);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS service add failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "mDNS registered: %s._esphomelib._tcp.local:%d",
             instance, s_api.config.port);

    return ESP_OK;
}

static void mdns_stop(void)
{
    mdns_service_remove_all();
    mdns_free();
}
#endif

/* ============================================================================
 * TCP Server (Framework)
 * ============================================================================ */

#ifndef TEST_HOST
static void server_task(void *arg)
{
    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket < 0) {
        ESP_LOGE(TAG, "Socket create failed");
        vTaskDelete(NULL);
        return;
    }

    /* Allow address reuse */
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind */
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(s_api.config.port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed");
        close(server_socket);
        vTaskDelete(NULL);
        return;
    }

    /* Listen */
    if (listen(server_socket, 1) < 0) {
        ESP_LOGE(TAG, "Socket listen failed");
        close(server_socket);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Server listening on port %d", s_api.config.port);

    while (s_api.running) {
        /* Accept connection */
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        /* Set accept timeout */
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int client = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client < 0) {
            continue;  /* Timeout or error, check running flag */
        }

        s_api.client_socket = client;
        s_api.stats.connections_total++;
        set_conn_state(CONN_STATE_CONNECTED);

        ESP_LOGI(TAG, "Client connected from %s",
                 inet_ntoa(client_addr.sin_addr));

        /* Handle connection */
        /* TODO: Implement ESPHome protocol handling */
        /* For now, just keep connection alive */

        uint8_t buf[256];
        while (s_api.running && s_api.client_socket >= 0) {
            ssize_t len = recv(client, buf, sizeof(buf), MSG_DONTWAIT);
            if (len > 0) {
                s_api.stats.messages_received++;
                /* TODO: Process protobuf message */
            } else if (len == 0) {
                /* Client disconnected */
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        close(client);
        s_api.client_socket = -1;
        set_conn_state(CONN_STATE_DISCONNECTED);
        ESP_LOGI(TAG, "Client disconnected");
    }

    close(server_socket);
    vTaskDelete(NULL);
}
#endif

/* ============================================================================
 * Public API: Initialization
 * ============================================================================ */

esp_err_t native_api_init(const native_api_config_t *config,
                           const native_api_device_info_t *device_info)
{
    if (s_api.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (!device_info) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Apply configuration */
    if (config) {
        s_api.config = *config;
    } else {
        native_api_config_t defaults = NATIVE_API_CONFIG_DEFAULT();
        s_api.config = defaults;
    }

    memcpy(&s_api.device_info, device_info, sizeof(native_api_device_info_t));

    /* Initialize state */
    s_api.entity_count = 0;
    s_api.zone_count = 0;
    s_api.conn_state = CONN_STATE_DISCONNECTED;
    s_api.client_socket = -1;
    memset(&s_api.stats, 0, sizeof(s_api.stats));

    s_api.initialized = true;
    ESP_LOGI(TAG, "Initialized (port=%d)", s_api.config.port);

    return ESP_OK;
}

void native_api_deinit(void)
{
    if (!s_api.initialized) {
        return;
    }

    native_api_stop();
    memset(&s_api, 0, sizeof(s_api));
    ESP_LOGI(TAG, "Deinitialized");
}

esp_err_t native_api_start(void)
{
    if (!s_api.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_api.running) {
        return ESP_OK;
    }

#ifndef TEST_HOST
    /* Start mDNS */
    esp_err_t err = mdns_start();
    if (err != ESP_OK) {
        return err;
    }

    /* Start server task */
    s_api.running = true;
    s_api.server_start_ms = timebase_uptime_ms();

    BaseType_t ret = xTaskCreate(server_task, "native_api",
                                  4096, NULL, 5, &s_api.server_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server task");
        mdns_stop();
        s_api.running = false;
        return ESP_FAIL;
    }
#else
    s_api.running = true;
    s_api.server_start_ms = timebase_uptime_ms();
#endif

    ESP_LOGI(TAG, "Server started");
    return ESP_OK;
}

void native_api_stop(void)
{
    if (!s_api.running) {
        return;
    }

    s_api.running = false;

#ifndef TEST_HOST
    /* Close client connection */
    if (s_api.client_socket >= 0) {
        close(s_api.client_socket);
        s_api.client_socket = -1;
    }

    /* Wait for task to finish */
    if (s_api.server_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_api.server_task = NULL;
    }

    mdns_stop();
#endif

    set_conn_state(CONN_STATE_DISCONNECTED);
    ESP_LOGI(TAG, "Server stopped");
}

bool native_api_is_running(void)
{
    return s_api.running;
}

/* ============================================================================
 * Public API: Entity Management
 * ============================================================================ */

uint32_t native_api_register_binary_sensor(const char *object_id,
                                            const char *name,
                                            const char *device_class,
                                            const char *icon)
{
    if (!object_id || s_api.entity_count >= NATIVE_API_MAX_ENTITIES) {
        return 0;
    }

    uint32_t key = fnv1a_hash(object_id);

    /* Check for duplicate */
    if (find_entity(key)) {
        ESP_LOGW(TAG, "Entity '%s' already registered", object_id);
        return key;
    }

    entity_def_t *ent = &s_api.entities[s_api.entity_count];
    memset(ent, 0, sizeof(entity_def_t));

    ent->key = key;
    ent->type = ENTITY_TYPE_BINARY_SENSOR;
    strncpy(ent->object_id, object_id, NATIVE_API_OBJECT_ID_LEN - 1);
    if (name) strncpy(ent->name, name, NATIVE_API_NAME_LEN - 1);
    if (device_class) strncpy(ent->device_class, device_class, NATIVE_API_DEVICE_CLASS_LEN - 1);
    if (icon) strncpy(ent->icon, icon, NATIVE_API_ICON_LEN - 1);
    ent->enabled = true;

    /* Initialize state cache */
    s_api.state_cache[s_api.entity_count].key = key;
    s_api.state_cache[s_api.entity_count].state.binary = false;
    s_api.state_cache[s_api.entity_count].last_publish_ms = 0;

    s_api.entity_count++;

    ESP_LOGD(TAG, "Registered binary_sensor '%s' (key=0x%08lx)",
             object_id, (unsigned long)key);

    return key;
}

uint32_t native_api_register_sensor(const char *object_id,
                                     const char *name,
                                     const char *device_class,
                                     const char *unit,
                                     const char *icon)
{
    if (!object_id || s_api.entity_count >= NATIVE_API_MAX_ENTITIES) {
        return 0;
    }

    uint32_t key = fnv1a_hash(object_id);

    if (find_entity(key)) {
        ESP_LOGW(TAG, "Entity '%s' already registered", object_id);
        return key;
    }

    entity_def_t *ent = &s_api.entities[s_api.entity_count];
    memset(ent, 0, sizeof(entity_def_t));

    ent->key = key;
    ent->type = ENTITY_TYPE_SENSOR;
    strncpy(ent->object_id, object_id, NATIVE_API_OBJECT_ID_LEN - 1);
    if (name) strncpy(ent->name, name, NATIVE_API_NAME_LEN - 1);
    if (device_class) strncpy(ent->device_class, device_class, NATIVE_API_DEVICE_CLASS_LEN - 1);
    if (unit) strncpy(ent->unit, unit, NATIVE_API_UNIT_LEN - 1);
    if (icon) strncpy(ent->icon, icon, NATIVE_API_ICON_LEN - 1);
    ent->enabled = true;

    s_api.state_cache[s_api.entity_count].key = key;
    s_api.state_cache[s_api.entity_count].state.value = 0.0f;
    s_api.state_cache[s_api.entity_count].last_publish_ms = 0;

    s_api.entity_count++;

    ESP_LOGD(TAG, "Registered sensor '%s' (key=0x%08lx)",
             object_id, (unsigned long)key);

    return key;
}

esp_err_t native_api_unregister_entity(uint32_t key)
{
    for (int i = 0; i < s_api.entity_count; i++) {
        if (s_api.entities[i].key == key) {
            s_api.entities[i].enabled = false;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void native_api_clear_entities(void)
{
    s_api.entity_count = 0;
    s_api.zone_count = 0;
    memset(s_api.entities, 0, sizeof(s_api.entities));
    memset(s_api.state_cache, 0, sizeof(s_api.state_cache));
    memset(s_api.zone_keys, 0, sizeof(s_api.zone_keys));
}

uint8_t native_api_get_entity_count(void)
{
    return s_api.entity_count;
}

/* ============================================================================
 * Public API: State Publishing
 * ============================================================================ */

esp_err_t native_api_publish_binary_state(uint32_t key, bool state)
{
    if (!s_api.running) {
        return ESP_ERR_INVALID_STATE;
    }

    entity_def_t *ent = find_entity(key);
    if (!ent || ent->type != ENTITY_TYPE_BINARY_SENSOR) {
        return ESP_ERR_NOT_FOUND;
    }

    int idx = find_state_cache_idx(key);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Check throttle */
    uint32_t now = timebase_uptime_ms();
    if (now - s_api.state_cache[idx].last_publish_ms < s_api.config.state_throttle_ms) {
        s_api.stats.state_updates_throttled++;
        return ESP_OK;
    }

    /* Update cache */
    s_api.state_cache[idx].state.binary = state;
    s_api.state_cache[idx].last_publish_ms = now;

    /* Send to client (if subscribed) */
    if (s_api.conn_state == CONN_STATE_SUBSCRIBED) {
        /* TODO: Send protobuf BinarySensorStateResponse */
        s_api.stats.messages_sent++;
    }

    s_api.stats.state_updates++;
    return ESP_OK;
}

esp_err_t native_api_publish_sensor_state(uint32_t key, float value)
{
    if (!s_api.running) {
        return ESP_ERR_INVALID_STATE;
    }

    entity_def_t *ent = find_entity(key);
    if (!ent || ent->type != ENTITY_TYPE_SENSOR) {
        return ESP_ERR_NOT_FOUND;
    }

    int idx = find_state_cache_idx(key);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t now = timebase_uptime_ms();
    if (now - s_api.state_cache[idx].last_publish_ms < s_api.config.state_throttle_ms) {
        s_api.stats.state_updates_throttled++;
        return ESP_OK;
    }

    s_api.state_cache[idx].state.value = value;
    s_api.state_cache[idx].last_publish_ms = now;

    if (s_api.conn_state == CONN_STATE_SUBSCRIBED) {
        /* TODO: Send protobuf SensorStateResponse */
        s_api.stats.messages_sent++;
    }

    s_api.stats.state_updates++;
    return ESP_OK;
}

esp_err_t native_api_publish_zones(const smoothed_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < frame->zone_count; i++) {
        const zone_smoothed_state_t *zone = &frame->zones[i];

        /* Find zone keys */
        for (int j = 0; j < s_api.zone_count; j++) {
            if (strcmp(s_api.zone_keys[j].zone_id, zone->zone_id) == 0) {
                native_api_publish_binary_state(s_api.zone_keys[j].occupancy_key,
                                                 zone->occupied);
                native_api_publish_sensor_state(s_api.zone_keys[j].count_key,
                                                 (float)zone->target_count);
                break;
            }
        }
    }

    return ESP_OK;
}

void native_api_publish_all_states(void)
{
    for (int i = 0; i < s_api.entity_count; i++) {
        if (!s_api.entities[i].enabled) continue;

        /* Force publish by clearing last_publish_ms */
        s_api.state_cache[i].last_publish_ms = 0;

        if (s_api.entities[i].type == ENTITY_TYPE_BINARY_SENSOR) {
            native_api_publish_binary_state(s_api.entities[i].key,
                                             s_api.state_cache[i].state.binary);
        } else if (s_api.entities[i].type == ENTITY_TYPE_SENSOR) {
            native_api_publish_sensor_state(s_api.entities[i].key,
                                             s_api.state_cache[i].state.value);
        }
    }
}

/* ============================================================================
 * Public API: Zone Entity Registration
 * ============================================================================ */

esp_err_t native_api_register_zone(const char *zone_id,
                                    const char *zone_name,
                                    uint32_t *occupancy_key,
                                    uint32_t *count_key)
{
    if (!zone_id || s_api.zone_count >= 16) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Build object IDs */
    char occ_id[32], cnt_id[32];
    char occ_name[48], cnt_name[48];

    snprintf(occ_id, sizeof(occ_id), "%s_occupancy", zone_id);
    snprintf(cnt_id, sizeof(cnt_id), "%s_target_count", zone_id);

    if (zone_name) {
        snprintf(occ_name, sizeof(occ_name), "%s Occupancy", zone_name);
        snprintf(cnt_name, sizeof(cnt_name), "%s Target Count", zone_name);
    } else {
        snprintf(occ_name, sizeof(occ_name), "%s Occupancy", zone_id);
        snprintf(cnt_name, sizeof(cnt_name), "%s Target Count", zone_id);
    }

    /* Register entities */
    uint32_t occ_key = native_api_register_binary_sensor(occ_id, occ_name,
                                                          "occupancy",
                                                          "mdi:motion-sensor");
    uint32_t cnt_key = native_api_register_sensor(cnt_id, cnt_name,
                                                   NULL, "", "mdi:account-multiple");

    if (occ_key == 0 || cnt_key == 0) {
        return ESP_ERR_NO_MEM;
    }

    /* Store mapping */
    strncpy(s_api.zone_keys[s_api.zone_count].zone_id, zone_id, 15);
    s_api.zone_keys[s_api.zone_count].occupancy_key = occ_key;
    s_api.zone_keys[s_api.zone_count].count_key = cnt_key;
    s_api.zone_count++;

    if (occupancy_key) *occupancy_key = occ_key;
    if (count_key) *count_key = cnt_key;

    ESP_LOGI(TAG, "Registered zone '%s' entities", zone_id);

    return ESP_OK;
}

/* ============================================================================
 * Public API: Connection Management
 * ============================================================================ */

void native_api_set_connection_callback(native_api_conn_callback_t callback,
                                         void *user_data)
{
    s_api.conn_callback = callback;
    s_api.callback_user_data = user_data;
}

esp_err_t native_api_get_connection_info(connection_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_api.conn_state == CONN_STATE_DISCONNECTED) {
        return ESP_ERR_NOT_FOUND;
    }

    info->state = s_api.conn_state;
    info->connected_at_ms = 0;  /* TODO: track */
    info->last_activity_ms = 0;
    info->messages_sent = s_api.stats.messages_sent;
    info->messages_received = s_api.stats.messages_received;
    info->client_info[0] = '\0';

    return ESP_OK;
}

bool native_api_has_subscriber(void)
{
    return s_api.conn_state == CONN_STATE_SUBSCRIBED;
}

void native_api_disconnect_client(void)
{
#ifndef TEST_HOST
    if (s_api.client_socket >= 0) {
        close(s_api.client_socket);
        s_api.client_socket = -1;
    }
#endif
    set_conn_state(CONN_STATE_DISCONNECTED);
}

/* ============================================================================
 * Public API: Statistics
 * ============================================================================ */

void native_api_get_stats(native_api_stats_t *stats)
{
    if (!stats) return;

    memcpy(stats, &s_api.stats, sizeof(native_api_stats_t));

    if (s_api.running) {
        stats->uptime_ms = timebase_uptime_ms() - s_api.server_start_ms;
    } else {
        stats->uptime_ms = 0;
    }
}

void native_api_reset_stats(void)
{
    memset(&s_api.stats, 0, sizeof(s_api.stats));
}
