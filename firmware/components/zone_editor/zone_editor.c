/**
 * @file zone_editor.c
 * @brief HardwareOS Zone Editor Interface Module (M11) Implementation
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_ZONE_EDITOR.md
 */

#include "zone_editor.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef TEST_HOST
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "config_store.h"
#include "zone_engine.h"
#else
/* Test host stubs */
#define ESP_LOGI(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...)
#define ESP_LOGE(tag, fmt, ...)
typedef void* httpd_handle_t;
typedef void* esp_timer_handle_t;
typedef int SemaphoreHandle_t;
#define portMAX_DELAY 0xFFFFFFFF
#endif

static const char *TAG = "zone_editor";

/* ============================================================================
 * Internal State
 * ============================================================================ */

typedef struct {
    int fd;                     /**< WebSocket file descriptor */
    bool active;
} ws_client_t;

typedef struct {
    bool initialized;
    bool running;
    zone_editor_config_t config;
    zone_editor_stats_t stats;
    zone_editor_event_cb_t callback;
    void *callback_user_data;

    /* Zone configuration */
    zone_config_t zone_config;

    /* Target streaming */
    target_frame_t current_frame;
    ws_client_t clients[ZONE_EDITOR_MAX_CLIENTS];
    int client_count;

    /* Authentication */
    char auth_token[64];
    bool auth_required;

#ifndef TEST_HOST
    httpd_handle_t httpd;
    esp_timer_handle_t stream_timer;
    SemaphoreHandle_t config_mutex;
    SemaphoreHandle_t frame_mutex;
#endif

} zone_editor_state_t;

static zone_editor_state_t s_state = {0};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void emit_event(zone_editor_event_t event);
static bool validate_polygon(const zone_vertex_t *vertices, int count);
static bool check_self_intersection(const zone_vertex_t *vertices, int count);
static bool lines_intersect(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                            int16_t x3, int16_t y3, int16_t x4, int16_t y4);

#ifndef TEST_HOST
static esp_err_t setup_http_server(void);
static void stream_timer_callback(void *arg);
static void broadcast_targets(void);

/* HTTP handlers */
static esp_err_t handle_get_zones(httpd_req_t *req);
static esp_err_t handle_post_zones(httpd_req_t *req);
static esp_err_t handle_get_targets(httpd_req_t *req);
static esp_err_t handle_ws_targets(httpd_req_t *req);
#endif

/* ============================================================================
 * Initialization
 * ============================================================================ */

esp_err_t zone_editor_init(const zone_editor_config_t *config)
{
    if (s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_state, 0, sizeof(s_state));

    if (config) {
        s_state.config = *config;
    } else {
        zone_editor_config_t defaults = ZONE_EDITOR_CONFIG_DEFAULT();
        s_state.config = defaults;
    }

    s_state.auth_required = s_state.config.require_auth;

#ifndef TEST_HOST
    /* Create mutexes */
    s_state.config_mutex = xSemaphoreCreateMutex();
    s_state.frame_mutex = xSemaphoreCreateMutex();

    if (!s_state.config_mutex || !s_state.frame_mutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return ESP_ERR_NO_MEM;
    }

    /* Create stream timer */
    uint32_t interval_us = 1000000 / s_state.config.stream_rate_hz;
    esp_timer_create_args_t timer_args = {
        .callback = stream_timer_callback,
        .name = "zone_stream"
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_state.stream_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create stream timer");
        return err;
    }

    /* Load zone config from config store */
    /* TODO: Load from M06 config store */
    s_state.zone_config.version = 1;
#endif

    s_state.initialized = true;
    ESP_LOGI(TAG, "Zone editor initialized");

    return ESP_OK;
}

void zone_editor_deinit(void)
{
    if (!s_state.initialized) {
        return;
    }

    zone_editor_stop();

#ifndef TEST_HOST
    if (s_state.stream_timer) {
        esp_timer_delete(s_state.stream_timer);
    }
    if (s_state.config_mutex) {
        vSemaphoreDelete(s_state.config_mutex);
    }
    if (s_state.frame_mutex) {
        vSemaphoreDelete(s_state.frame_mutex);
    }
#endif

    s_state.initialized = false;
    ESP_LOGI(TAG, "Zone editor deinitialized");
}

void zone_editor_set_callback(zone_editor_event_cb_t callback, void *user_data)
{
    s_state.callback = callback;
    s_state.callback_user_data = user_data;
}

esp_err_t zone_editor_start(void)
{
    if (!s_state.initialized || s_state.running) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef TEST_HOST
    esp_err_t err = setup_http_server();
    if (err != ESP_OK) {
        return err;
    }

    /* Start stream timer */
    uint32_t interval_us = 1000000 / s_state.config.stream_rate_hz;
    esp_timer_start_periodic(s_state.stream_timer, interval_us);
#endif

    s_state.running = true;
    ESP_LOGI(TAG, "Zone editor started on port %d", s_state.config.http_port);

    return ESP_OK;
}

void zone_editor_stop(void)
{
    if (!s_state.running) {
        return;
    }

#ifndef TEST_HOST
    esp_timer_stop(s_state.stream_timer);

    if (s_state.httpd) {
        httpd_stop(s_state.httpd);
        s_state.httpd = NULL;
    }
#endif

    s_state.running = false;
    ESP_LOGI(TAG, "Zone editor stopped");
}

/* ============================================================================
 * Zone Configuration
 * ============================================================================ */

esp_err_t zone_editor_get_config(zone_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.config_mutex, portMAX_DELAY);
#endif

    *config = s_state.zone_config;

#ifndef TEST_HOST
    xSemaphoreGive(s_state.config_mutex);
#endif

    return ESP_OK;
}

esp_err_t zone_editor_set_config(const zone_config_t *config,
                                  uint32_t expected_version)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.config_mutex, portMAX_DELAY);
#endif

    /* Check version for optimistic locking */
    if (expected_version != 0 &&
        expected_version != s_state.zone_config.version) {
        ESP_LOGW(TAG, "Version mismatch: expected %lu, current %lu",
                 (unsigned long)expected_version,
                 (unsigned long)s_state.zone_config.version);
#ifndef TEST_HOST
        xSemaphoreGive(s_state.config_mutex);
#endif
        s_state.stats.config_rejections++;
        emit_event(ZONE_EDITOR_EVENT_CONFIG_REJECTED);
        return ESP_ERR_INVALID_VERSION;
    }

    /* Validate configuration */
    int error_zone;
    zone_validation_t result = zone_editor_validate(config, &error_zone);
    if (result != ZONE_VALID_OK) {
        ESP_LOGW(TAG, "Validation failed: %s (zone %d)",
                 zone_editor_validation_str(result), error_zone);
#ifndef TEST_HOST
        xSemaphoreGive(s_state.config_mutex);
#endif
        s_state.stats.config_rejections++;
        emit_event(ZONE_EDITOR_EVENT_CONFIG_REJECTED);
        return ESP_ERR_INVALID_ARG;
    }

    /* Apply configuration */
    s_state.zone_config = *config;
    s_state.zone_config.version++;

    /* Update timestamp */
#ifndef TEST_HOST
    time_t now;
    time(&now);
    strftime(s_state.zone_config.updated_at,
             sizeof(s_state.zone_config.updated_at),
             "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
#endif

    /* Save to config store */
#ifndef TEST_HOST
    /* TODO: Save to M06 config store */
    /* config_store_save_zones(&s_state.zone_config); */
#endif

#ifndef TEST_HOST
    xSemaphoreGive(s_state.config_mutex);
#endif

    s_state.stats.config_updates++;
    emit_event(ZONE_EDITOR_EVENT_CONFIG_UPDATED);

    ESP_LOGI(TAG, "Zone config updated to version %lu",
             (unsigned long)s_state.zone_config.version);

    return ESP_OK;
}

uint32_t zone_editor_get_version(void)
{
    return s_state.zone_config.version;
}

/* ============================================================================
 * Target Streaming
 * ============================================================================ */

void zone_editor_update_targets(const target_frame_t *frame)
{
    if (!frame) {
        return;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.frame_mutex, portMAX_DELAY);
#endif

    s_state.current_frame = *frame;

#ifndef TEST_HOST
    xSemaphoreGive(s_state.frame_mutex);
#endif
}

int zone_editor_get_client_count(void)
{
    return s_state.client_count;
}

bool zone_editor_is_streaming(void)
{
    return s_state.client_count > 0;
}

/* ============================================================================
 * Coordinate Conversion
 * ============================================================================ */

int16_t zone_editor_meters_to_mm(float meters)
{
    float mm = meters * 1000.0f;

    /* Clamp to int16 range */
    if (mm > 32767.0f) mm = 32767.0f;
    if (mm < -32768.0f) mm = -32768.0f;

    return (int16_t)mm;
}

float zone_editor_mm_to_meters(int16_t mm)
{
    return mm / 1000.0f;
}

void zone_editor_config_to_mm(zone_config_t *config)
{
    /* Config is already in mm internally, this is a no-op */
    /* Conversion happens when parsing JSON from API */
    (void)config;
}

int zone_editor_config_to_json(const zone_config_t *config,
                                char *json_out, size_t json_len)
{
    if (!config || !json_out || json_len == 0) {
        return 0;
    }

#ifndef TEST_HOST
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return 0;
    }

    cJSON_AddNumberToObject(root, "version", config->version);
    cJSON_AddStringToObject(root, "updated_at", config->updated_at);

    cJSON *zones = cJSON_CreateArray();
    for (int i = 0; i < config->zone_count; i++) {
        const zone_def_t *z = &config->zones[i];

        cJSON *zone = cJSON_CreateObject();
        cJSON_AddStringToObject(zone, "id", z->id);
        cJSON_AddStringToObject(zone, "name", z->name);
        cJSON_AddStringToObject(zone, "type",
                                z->type == ZONE_TYPE_INCLUDE ? "include" : "exclude");
        cJSON_AddNumberToObject(zone, "sensitivity", z->sensitivity);

        /* Convert vertices to meters */
        cJSON *vertices = cJSON_CreateArray();
        for (int j = 0; j < z->vertex_count; j++) {
            cJSON *vertex = cJSON_CreateArray();
            cJSON_AddItemToArray(vertex,
                cJSON_CreateNumber(zone_editor_mm_to_meters(z->vertices[j].x)));
            cJSON_AddItemToArray(vertex,
                cJSON_CreateNumber(zone_editor_mm_to_meters(z->vertices[j].y)));
            cJSON_AddItemToArray(vertices, vertex);
        }
        cJSON_AddItemToObject(zone, "vertices", vertices);

        cJSON_AddItemToArray(zones, zone);
    }
    cJSON_AddItemToObject(root, "zones", zones);

    char *str = cJSON_PrintUnformatted(root);
    int len = 0;
    if (str) {
        len = strlen(str);
        if ((size_t)len < json_len) {
            strcpy(json_out, str);
        } else {
            len = 0;
        }
        free(str);
    }

    cJSON_Delete(root);
    return len;
#else
    return snprintf(json_out, json_len, "{\"version\":%lu,\"zones\":[]}",
                    (unsigned long)config->version);
#endif
}

/* ============================================================================
 * Validation
 * ============================================================================ */

zone_validation_t zone_editor_validate(const zone_config_t *config,
                                        int *error_zone)
{
    if (!config) {
        return ZONE_VALID_TOO_MANY_ZONES;
    }

    if (error_zone) {
        *error_zone = -1;
    }

    /* Check zone count */
    if (config->zone_count > ZONE_EDITOR_MAX_ZONES) {
        return ZONE_VALID_TOO_MANY_ZONES;
    }

    /* Check for duplicate IDs */
    for (int i = 0; i < config->zone_count; i++) {
        for (int j = i + 1; j < config->zone_count; j++) {
            if (strcmp(config->zones[i].id, config->zones[j].id) == 0) {
                if (error_zone) *error_zone = j;
                return ZONE_VALID_DUPLICATE_ID;
            }
        }
    }

    /* Validate each zone */
    for (int i = 0; i < config->zone_count; i++) {
        const zone_def_t *z = &config->zones[i];

        /* Check name */
        if (strlen(z->name) == 0) {
            if (error_zone) *error_zone = i;
            return ZONE_VALID_INVALID_NAME;
        }

        /* Check vertex count */
        if (z->vertex_count < 3) {
            if (error_zone) *error_zone = i;
            return ZONE_VALID_TOO_FEW_VERTICES;
        }
        if (z->vertex_count > ZONE_EDITOR_MAX_VERTICES) {
            if (error_zone) *error_zone = i;
            return ZONE_VALID_TOO_MANY_VERTICES;
        }

        /* Check coordinate range */
        for (int j = 0; j < z->vertex_count; j++) {
            if (z->vertices[j].x < ZONE_EDITOR_MIN_RANGE_MM ||
                z->vertices[j].x > ZONE_EDITOR_MAX_RANGE_MM ||
                z->vertices[j].y < ZONE_EDITOR_MIN_RANGE_MM ||
                z->vertices[j].y > ZONE_EDITOR_MAX_RANGE_MM) {
                if (error_zone) *error_zone = i;
                return ZONE_VALID_OUT_OF_RANGE;
            }
        }

        /* Check self-intersection */
        if (check_self_intersection(z->vertices, z->vertex_count)) {
            if (error_zone) *error_zone = i;
            return ZONE_VALID_SELF_INTERSECTING;
        }
    }

    return ZONE_VALID_OK;
}

const char *zone_editor_validation_str(zone_validation_t error)
{
    switch (error) {
        case ZONE_VALID_OK:                 return "Valid";
        case ZONE_VALID_TOO_FEW_VERTICES:   return "Too few vertices (min 3)";
        case ZONE_VALID_TOO_MANY_VERTICES:  return "Too many vertices (max 8)";
        case ZONE_VALID_SELF_INTERSECTING:  return "Self-intersecting polygon";
        case ZONE_VALID_OUT_OF_RANGE:       return "Vertex out of range";
        case ZONE_VALID_DUPLICATE_ID:       return "Duplicate zone ID";
        case ZONE_VALID_INVALID_NAME:       return "Invalid zone name";
        case ZONE_VALID_TOO_MANY_ZONES:     return "Too many zones (max 16)";
        case ZONE_VALID_VERSION_MISMATCH:   return "Version mismatch";
        default:                            return "Unknown error";
    }
}

/* ============================================================================
 * Authentication
 * ============================================================================ */

void zone_editor_set_auth_token(const char *token)
{
    if (token) {
        strncpy(s_state.auth_token, token, sizeof(s_state.auth_token) - 1);
        s_state.auth_required = true;
    } else {
        s_state.auth_token[0] = '\0';
        s_state.auth_required = false;
    }
}

bool zone_editor_check_auth(const char *auth_header)
{
    if (!s_state.auth_required) {
        return true;
    }

    if (!auth_header || strlen(s_state.auth_token) == 0) {
        return false;
    }

    /* Expect "Bearer <token>" format */
    if (strncmp(auth_header, "Bearer ", 7) != 0) {
        return false;
    }

    return strcmp(auth_header + 7, s_state.auth_token) == 0;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

void zone_editor_get_stats(zone_editor_stats_t *stats)
{
    if (stats) {
        *stats = s_state.stats;
        stats->clients_connected = s_state.client_count;
    }
}

void zone_editor_reset_stats(void)
{
    uint32_t clients = s_state.stats.clients_connected;
    memset(&s_state.stats, 0, sizeof(s_state.stats));
    s_state.stats.clients_connected = clients;
}

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

static void emit_event(zone_editor_event_t event)
{
    if (s_state.callback) {
        s_state.callback(event, s_state.callback_user_data);
    }
}

static bool check_self_intersection(const zone_vertex_t *vertices, int count)
{
    if (count < 4) {
        return false;  /* Triangle can't self-intersect */
    }

    /* Check each edge against non-adjacent edges */
    for (int i = 0; i < count; i++) {
        int i_next = (i + 1) % count;

        for (int j = i + 2; j < count; j++) {
            /* Skip adjacent edges */
            if (j == count - 1 && i == 0) {
                continue;
            }

            int j_next = (j + 1) % count;

            if (lines_intersect(
                    vertices[i].x, vertices[i].y,
                    vertices[i_next].x, vertices[i_next].y,
                    vertices[j].x, vertices[j].y,
                    vertices[j_next].x, vertices[j_next].y)) {
                return true;
            }
        }
    }

    return false;
}

static bool lines_intersect(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                            int16_t x3, int16_t y3, int16_t x4, int16_t y4)
{
    /* Line segment intersection using cross products */
    int32_t d1 = (int32_t)(x4 - x3) * (y1 - y3) - (int32_t)(y4 - y3) * (x1 - x3);
    int32_t d2 = (int32_t)(x4 - x3) * (y2 - y3) - (int32_t)(y4 - y3) * (x2 - x3);
    int32_t d3 = (int32_t)(x2 - x1) * (y3 - y1) - (int32_t)(y2 - y1) * (x3 - x1);
    int32_t d4 = (int32_t)(x2 - x1) * (y4 - y1) - (int32_t)(y2 - y1) * (x4 - x1);

    /* Check if segments straddle each other */
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
        ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        return true;
    }

    return false;
}

#ifndef TEST_HOST

static esp_err_t setup_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = s_state.config.http_port;
    config.max_open_sockets = s_state.config.max_clients + 2;

    esp_err_t err = httpd_start(&s_state.httpd, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    /* Register handlers */
    httpd_uri_t get_zones = {
        .uri = "/api/zones",
        .method = HTTP_GET,
        .handler = handle_get_zones,
    };
    httpd_register_uri_handler(s_state.httpd, &get_zones);

    httpd_uri_t post_zones = {
        .uri = "/api/zones",
        .method = HTTP_POST,
        .handler = handle_post_zones,
    };
    httpd_register_uri_handler(s_state.httpd, &post_zones);

    httpd_uri_t get_targets = {
        .uri = "/api/targets",
        .method = HTTP_GET,
        .handler = handle_get_targets,
    };
    httpd_register_uri_handler(s_state.httpd, &get_targets);

    httpd_uri_t ws_targets = {
        .uri = "/api/targets/stream",
        .method = HTTP_GET,
        .handler = handle_ws_targets,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_state.httpd, &ws_targets);

    return ESP_OK;
}

static void stream_timer_callback(void *arg)
{
    (void)arg;

    if (s_state.client_count > 0) {
        broadcast_targets();
    }
}

static void broadcast_targets(void)
{
    if (s_state.client_count == 0) {
        return;
    }

    xSemaphoreTake(s_state.frame_mutex, portMAX_DELAY);
    target_frame_t frame = s_state.current_frame;
    xSemaphoreGive(s_state.frame_mutex);

    /* Build JSON message */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", frame.timestamp_ms);
    cJSON_AddNumberToObject(root, "seq", frame.frame_seq);

    cJSON *targets = cJSON_CreateArray();
    for (int i = 0; i < frame.target_count && i < ZONE_EDITOR_MAX_TARGETS; i++) {
        if (!frame.targets[i].active) {
            continue;
        }

        cJSON *target = cJSON_CreateObject();
        cJSON_AddNumberToObject(target, "id", frame.targets[i].track_id);
        cJSON_AddNumberToObject(target, "x",
            zone_editor_mm_to_meters(frame.targets[i].x));
        cJSON_AddNumberToObject(target, "y",
            zone_editor_mm_to_meters(frame.targets[i].y));
        cJSON_AddNumberToObject(target, "vx",
            zone_editor_mm_to_meters(frame.targets[i].vx));
        cJSON_AddNumberToObject(target, "vy",
            zone_editor_mm_to_meters(frame.targets[i].vy));
        cJSON_AddNumberToObject(target, "conf", frame.targets[i].confidence);
        cJSON_AddItemToArray(targets, target);
    }
    cJSON_AddItemToObject(root, "targets", targets);

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        size_t len = strlen(json);

        /* Send to all connected clients */
        for (int i = 0; i < ZONE_EDITOR_MAX_CLIENTS; i++) {
            if (s_state.clients[i].active) {
                httpd_ws_frame_t ws_pkt = {
                    .final = true,
                    .fragmented = false,
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t *)json,
                    .len = len,
                };

                esp_err_t err = httpd_ws_send_frame_async(
                    s_state.httpd, s_state.clients[i].fd, &ws_pkt);

                if (err == ESP_OK) {
                    s_state.stats.ws_frames_sent++;
                } else {
                    s_state.stats.ws_frames_dropped++;
                }
            }
        }

        free(json);
    }

    cJSON_Delete(root);
}

/* HTTP Handlers */

static esp_err_t handle_get_zones(httpd_req_t *req)
{
    s_state.stats.requests_total++;

    /* Check auth */
    char auth_header[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header,
                                     sizeof(auth_header)) == ESP_OK) {
        if (!zone_editor_check_auth(auth_header)) {
            s_state.stats.requests_auth_failed++;
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
            return ESP_FAIL;
        }
    } else if (s_state.auth_required) {
        s_state.stats.requests_auth_failed++;
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    /* Get config */
    zone_config_t config;
    zone_editor_get_config(&config);

    /* Convert to JSON */
    char *json = malloc(4096);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int len = zone_editor_config_to_json(&config, json, 4096);
    if (len > 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, len);
        s_state.stats.requests_success++;
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON error");
    }

    free(json);
    return ESP_OK;
}

static esp_err_t handle_post_zones(httpd_req_t *req)
{
    s_state.stats.requests_total++;

    /* Check auth */
    char auth_header[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header,
                                     sizeof(auth_header)) == ESP_OK) {
        if (!zone_editor_check_auth(auth_header)) {
            s_state.stats.requests_auth_failed++;
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
            return ESP_FAIL;
        }
    } else if (s_state.auth_required) {
        s_state.stats.requests_auth_failed++;
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    /* Read body */
    int total_len = req->content_len;
    if (total_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    char *buf = malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, buf, total_len);
    if (received != total_len) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        return ESP_FAIL;
    }
    buf[total_len] = '\0';

    /* Parse JSON */
    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    zone_config_t config = {0};

    /* Parse version for optimistic locking */
    cJSON *version = cJSON_GetObjectItem(root, "version");
    uint32_t expected_version = 0;
    if (cJSON_IsNumber(version)) {
        expected_version = (uint32_t)version->valuedouble;
    }

    /* Parse zones */
    cJSON *zones = cJSON_GetObjectItem(root, "zones");
    if (cJSON_IsArray(zones)) {
        int zone_count = cJSON_GetArraySize(zones);
        if (zone_count > ZONE_EDITOR_MAX_ZONES) {
            zone_count = ZONE_EDITOR_MAX_ZONES;
        }

        for (int i = 0; i < zone_count; i++) {
            cJSON *zone = cJSON_GetArrayItem(zones, i);
            zone_def_t *z = &config.zones[i];

            cJSON *id = cJSON_GetObjectItem(zone, "id");
            if (cJSON_IsString(id)) {
                strncpy(z->id, id->valuestring, ZONE_EDITOR_MAX_ID_LEN - 1);
            }

            cJSON *name = cJSON_GetObjectItem(zone, "name");
            if (cJSON_IsString(name)) {
                strncpy(z->name, name->valuestring, ZONE_EDITOR_MAX_NAME_LEN - 1);
            }

            cJSON *type = cJSON_GetObjectItem(zone, "type");
            if (cJSON_IsString(type)) {
                z->type = strcmp(type->valuestring, "exclude") == 0 ?
                          ZONE_TYPE_EXCLUDE : ZONE_TYPE_INCLUDE;
            }

            cJSON *sensitivity = cJSON_GetObjectItem(zone, "sensitivity");
            z->sensitivity = cJSON_IsNumber(sensitivity) ?
                             (uint8_t)sensitivity->valuedouble : 50;

            cJSON *vertices = cJSON_GetObjectItem(zone, "vertices");
            if (cJSON_IsArray(vertices)) {
                int vert_count = cJSON_GetArraySize(vertices);
                if (vert_count > ZONE_EDITOR_MAX_VERTICES) {
                    vert_count = ZONE_EDITOR_MAX_VERTICES;
                }

                for (int j = 0; j < vert_count; j++) {
                    cJSON *vertex = cJSON_GetArrayItem(vertices, j);
                    if (cJSON_IsArray(vertex) && cJSON_GetArraySize(vertex) >= 2) {
                        /* Convert from meters to mm */
                        float x = (float)cJSON_GetArrayItem(vertex, 0)->valuedouble;
                        float y = (float)cJSON_GetArrayItem(vertex, 1)->valuedouble;
                        z->vertices[j].x = zone_editor_meters_to_mm(x);
                        z->vertices[j].y = zone_editor_meters_to_mm(y);
                    }
                }
                z->vertex_count = vert_count;
            }

            config.zone_count++;
        }
    }

    cJSON_Delete(root);

    /* Apply config */
    esp_err_t err = zone_editor_set_config(&config, expected_version);
    if (err == ESP_OK) {
        s_state.stats.requests_success++;
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else if (err == ESP_ERR_INVALID_VERSION) {
        httpd_resp_send_err(req, HTTPD_409_CONFLICT, "Version conflict");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Validation failed");
    }

    return ESP_OK;
}

static esp_err_t handle_get_targets(httpd_req_t *req)
{
    s_state.stats.requests_total++;

    xSemaphoreTake(s_state.frame_mutex, portMAX_DELAY);
    target_frame_t frame = s_state.current_frame;
    xSemaphoreGive(s_state.frame_mutex);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", frame.timestamp_ms);

    cJSON *targets = cJSON_CreateArray();
    for (int i = 0; i < frame.target_count; i++) {
        if (!frame.targets[i].active) continue;

        cJSON *target = cJSON_CreateObject();
        cJSON_AddNumberToObject(target, "x",
            zone_editor_mm_to_meters(frame.targets[i].x));
        cJSON_AddNumberToObject(target, "y",
            zone_editor_mm_to_meters(frame.targets[i].y));
        cJSON_AddNumberToObject(target, "confidence", frame.targets[i].confidence);
        cJSON_AddItemToArray(targets, target);
    }
    cJSON_AddItemToObject(root, "targets", targets);

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        s_state.stats.requests_success++;
        free(json);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_ws_targets(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake */
        ESP_LOGI(TAG, "WebSocket client connecting");

        /* Find free client slot */
        int slot = -1;
        for (int i = 0; i < ZONE_EDITOR_MAX_CLIENTS; i++) {
            if (!s_state.clients[i].active) {
                slot = i;
                break;
            }
        }

        if (slot < 0) {
            ESP_LOGW(TAG, "No free client slots");
            return ESP_FAIL;
        }

        s_state.clients[slot].fd = httpd_req_to_sockfd(req);
        s_state.clients[slot].active = true;
        s_state.client_count++;

        emit_event(ZONE_EDITOR_EVENT_CLIENT_CONNECTED);
        if (s_state.client_count == 1) {
            emit_event(ZONE_EDITOR_EVENT_STREAM_STARTED);
        }

        ESP_LOGI(TAG, "WebSocket client connected (slot %d, total %d)",
                 slot, s_state.client_count);
    }

    return ESP_OK;
}

#endif /* TEST_HOST */
