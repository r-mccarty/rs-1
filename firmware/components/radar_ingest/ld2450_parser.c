/**
 * @file ld2450_parser.c
 * @brief LD2450 24GHz mmWave Radar Frame Parser Implementation
 *
 * Implements the frame parsing logic for the HiLink LD2450 radar.
 *
 * Frame Structure (40 bytes total):
 * ┌─────────────────────────────────────────────────────────────────┐
 * │ Offset │  Size  │  Field          │  Description               │
 * ├────────┼────────┼─────────────────┼────────────────────────────┤
 * │   0-3  │   4    │  Header         │  0xAA 0xFF 0x03 0x00       │
 * │   4-5  │   2    │  Target 1 X     │  Signed int16, mm          │
 * │   6-7  │   2    │  Target 1 Y     │  Signed int16, mm          │
 * │   8-9  │   2    │  Target 1 Speed │  Signed int16, cm/s        │
 * │  10-11 │   2    │  Target 1 Res   │  uint16, mm resolution     │
 * │  12-23 │  12    │  Target 2       │  Same structure            │
 * │  24-35 │  12    │  Target 3       │  Same structure            │
 * │  36-37 │   2    │  Checksum       │  Sum of bytes 4-35         │
 * │  38-39 │   2    │  Footer         │  0x55 0xCC                 │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md
 */

#include "ld2450_parser.h"
#include "esp_timer.h"
#include <string.h>

/* ============================================================================
 * Private Constants
 * ============================================================================ */

/* Byte offsets in frame */
#define OFFSET_HEADER       0
#define OFFSET_TARGET1      4
#define OFFSET_TARGET2      12
#define OFFSET_TARGET3      24
#define OFFSET_CHECKSUM     36
#define OFFSET_FOOTER       38

/* Target field offsets (relative to target start) */
#define TARGET_OFFSET_X         0
#define TARGET_OFFSET_Y         2
#define TARGET_OFFSET_SPEED     4
#define TARGET_OFFSET_RES       6

/* Header sequence */
static const uint8_t LD2450_HEADER[] = {
    LD2450_HEADER_0, LD2450_HEADER_1, LD2450_HEADER_2, LD2450_HEADER_3
};

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * @brief Read signed 16-bit little-endian value
 */
static inline int16_t read_int16_le(const uint8_t *buf)
{
    return (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

/**
 * @brief Read unsigned 16-bit little-endian value
 */
static inline uint16_t read_uint16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/**
 * @brief Calculate checksum over frame data
 *
 * Checksum is the sum of bytes from offset 4 to 35 (inclusive),
 * stored as little-endian uint16 at offset 36-37.
 */
static uint16_t calculate_checksum(const uint8_t *buffer)
{
    uint16_t sum = 0;
    for (int i = OFFSET_TARGET1; i < OFFSET_CHECKSUM; i++) {
        sum += buffer[i];
    }
    return sum;
}

/**
 * @brief Parse a single target from the frame buffer
 */
static void parse_target(const uint8_t *buf, radar_detection_t *target)
{
    int16_t x = read_int16_le(buf + TARGET_OFFSET_X);
    int16_t y = read_int16_le(buf + TARGET_OFFSET_Y);
    int16_t speed = read_int16_le(buf + TARGET_OFFSET_SPEED);
    uint16_t res = read_uint16_le(buf + TARGET_OFFSET_RES);

    /* Check for invalid/empty target marker
     * LD2450 uses 0x8000 or all zeros to indicate no target
     */
    if (x == (int16_t)LD2450_INVALID_COORD ||
        (x == 0 && y == 0 && speed == 0 && res == 0)) {
        target->valid = false;
        target->x_mm = 0;
        target->y_mm = 0;
        target->speed_cm_s = 0;
        target->resolution_mm = 0;
        target->signal_quality = 0;
        return;
    }

    target->x_mm = x;
    target->y_mm = y;
    target->speed_cm_s = speed;
    target->resolution_mm = res;
    target->valid = true;

    /* Derive signal quality from resolution
     * Lower resolution = better signal quality
     * Map 0-1000mm resolution to 100-0 quality
     */
    if (res <= 100) {
        target->signal_quality = 100;
    } else if (res >= 1000) {
        target->signal_quality = 0;
    } else {
        target->signal_quality = (uint8_t)(100 - (res - 100) * 100 / 900);
    }
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

void ld2450_parser_init(ld2450_parser_t *parser)
{
    memset(parser, 0, sizeof(ld2450_parser_t));
    parser->state = LD2450_STATE_WAIT_HEADER;
}

void ld2450_parser_reset(ld2450_parser_t *parser)
{
    parser->buffer_idx = 0;
    parser->header_matched = 0;
    parser->state = LD2450_STATE_WAIT_HEADER;
    parser->sync_lost++;
}

bool ld2450_parser_validate_checksum(const uint8_t *buffer, size_t len)
{
    if (len < LD2450_FRAME_SIZE) {
        return false;
    }

    uint16_t calculated = calculate_checksum(buffer);
    uint16_t received = read_uint16_le(buffer + OFFSET_CHECKSUM);

    /* Some LD2450 firmware versions send 0x0000 checksum - accept as valid */
    if (received == 0) {
        return true;
    }

    return calculated == received;
}

bool ld2450_parser_parse_frame(const uint8_t *buffer,
                               radar_detection_frame_t *frame)
{
    /* Validate header */
    if (memcmp(buffer, LD2450_HEADER, LD2450_HEADER_SIZE) != 0) {
        return false;
    }

    /* Validate footer */
    if (buffer[OFFSET_FOOTER] != LD2450_FOOTER_0 ||
        buffer[OFFSET_FOOTER + 1] != LD2450_FOOTER_1) {
        return false;
    }

    /* Validate checksum */
    if (!ld2450_parser_validate_checksum(buffer, LD2450_FRAME_SIZE)) {
        return false;
    }

    /* Parse targets */
    parse_target(buffer + OFFSET_TARGET1, &frame->targets[0]);
    parse_target(buffer + OFFSET_TARGET2, &frame->targets[1]);
    parse_target(buffer + OFFSET_TARGET3, &frame->targets[2]);

    /* Count valid targets */
    frame->target_count = 0;
    for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
        if (frame->targets[i].valid) {
            frame->target_count++;
        }
    }

    /* Set timestamp */
    frame->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    return true;
}

bool ld2450_parser_feed(ld2450_parser_t *parser,
                        const uint8_t *data,
                        size_t len,
                        radar_detection_frame_t *frame)
{
    bool frame_ready = false;

    for (size_t i = 0; i < len && !frame_ready; i++) {
        uint8_t byte = data[i];

        switch (parser->state) {
            case LD2450_STATE_WAIT_HEADER:
                /* Match header sequence byte by byte */
                if (byte == LD2450_HEADER[parser->header_matched]) {
                    parser->buffer[parser->header_matched] = byte;
                    parser->header_matched++;

                    if (parser->header_matched == LD2450_HEADER_SIZE) {
                        /* Header complete, start receiving data */
                        parser->buffer_idx = LD2450_HEADER_SIZE;
                        parser->state = LD2450_STATE_RECEIVE_DATA;
                    }
                } else {
                    /* Mismatch - check if this byte starts a new header */
                    parser->header_matched = 0;
                    if (byte == LD2450_HEADER[0]) {
                        parser->buffer[0] = byte;
                        parser->header_matched = 1;
                    }
                }
                break;

            case LD2450_STATE_RECEIVE_DATA:
                parser->buffer[parser->buffer_idx++] = byte;

                if (parser->buffer_idx >= LD2450_FRAME_SIZE) {
                    /* Frame complete - validate and parse */
                    if (ld2450_parser_parse_frame(parser->buffer, frame)) {
                        frame->frame_seq = parser->frame_seq++;
                        parser->frames_parsed++;
                        frame_ready = true;
                    } else {
                        parser->frames_invalid++;
                    }

                    /* Reset for next frame */
                    parser->buffer_idx = 0;
                    parser->header_matched = 0;
                    parser->state = LD2450_STATE_WAIT_HEADER;
                }
                break;

            default:
                ld2450_parser_reset(parser);
                break;
        }
    }

    return frame_ready;
}

void ld2450_parser_get_stats(const ld2450_parser_t *parser,
                             uint32_t *frames_parsed,
                             uint32_t *frames_invalid)
{
    if (frames_parsed) {
        *frames_parsed = parser->frames_parsed;
    }
    if (frames_invalid) {
        *frames_invalid = parser->frames_invalid;
    }
}
