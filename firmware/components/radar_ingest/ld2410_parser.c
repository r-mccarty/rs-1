/**
 * @file ld2410_parser.c
 * @brief LD2410 24GHz mmWave Radar Frame Parser Implementation
 *
 * Implements the frame parsing logic for the HiLink LD2410 radar
 * in Engineering Mode.
 *
 * Engineering Mode Frame Structure (39 bytes):
 * ┌─────────────────────────────────────────────────────────────────┐
 * │ Offset │  Size  │  Field               │  Description          │
 * ├────────┼────────┼──────────────────────┼───────────────────────┤
 * │   0-3  │   4    │  Header              │  0xF4 0xF3 0xF2 0xF1  │
 * │   4-5  │   2    │  Data Length         │  Little-endian        │
 * │   6    │   1    │  Data Type           │  0x01 = Engineering   │
 * │   7    │   1    │  Head                │  0xAA                 │
 * │   8    │   1    │  Target State        │  0x00-0x03            │
 * │   9-10 │   2    │  Moving Distance     │  cm, little-endian    │
 * │  11    │   1    │  Moving Energy       │  0-100                │
 * │  12-13 │   2    │  Stationary Distance │  cm, little-endian    │
 * │  14    │   1    │  Stationary Energy   │  0-100                │
 * │  15-16 │   2    │  Detection Distance  │  cm, little-endian    │
 * │  17-24 │   8    │  Moving Energy Gates │  Gates 0-7 (0-100)    │
 * │  25-32 │   8    │  Stationary Gates    │  Gates 0-7 (0-100)    │
 * │  33    │   1    │  Tail                │  0x55                 │
 * │  34    │   1    │  Check               │  0x00                 │
 * │  35-38 │   4    │  Footer              │  0xF8 0xF7 0xF6 0xF5  │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Note: Some firmware versions have 9 gates (offset changes).
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md Section 3.3
 */

#include "ld2410_parser.h"
#include "esp_timer.h"
#include <string.h>

/* ============================================================================
 * Private Constants
 * ============================================================================ */

/* Byte offsets in engineering mode frame */
#define OFFSET_HEADER           0
#define OFFSET_DATA_LEN         4
#define OFFSET_DATA_TYPE        6
#define OFFSET_HEAD             7
#define OFFSET_TARGET_STATE     8
#define OFFSET_MOVING_DIST      9
#define OFFSET_MOVING_ENERGY    11
#define OFFSET_STAT_DIST        12
#define OFFSET_STAT_ENERGY      14
#define OFFSET_DETECT_DIST      15
#define OFFSET_MOVING_GATES     17
#define OFFSET_STAT_GATES       25
#define OFFSET_TAIL             33
#define OFFSET_CHECK            34
#define OFFSET_FOOTER           35

/* Header and footer sequences */
static const uint8_t LD2410_HEADER[] = {
    LD2410_HEADER_0, LD2410_HEADER_1, LD2410_HEADER_2, LD2410_HEADER_3
};

static const uint8_t LD2410_FOOTER[] = {
    LD2410_FOOTER_0, LD2410_FOOTER_1, LD2410_FOOTER_2, LD2410_FOOTER_3
};

/* Command frame header/footer */
static const uint8_t CMD_HEADER[] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FOOTER[] = {0x04, 0x03, 0x02, 0x01};

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * @brief Read unsigned 16-bit little-endian value
 */
static inline uint16_t read_uint16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

void ld2410_parser_init(ld2410_parser_t *parser)
{
    memset(parser, 0, sizeof(ld2410_parser_t));
    parser->state = LD2410_STATE_WAIT_HEADER;
}

void ld2410_parser_reset(ld2410_parser_t *parser)
{
    parser->buffer_idx = 0;
    parser->header_matched = 0;
    parser->expected_len = 0;
    parser->state = LD2410_STATE_WAIT_HEADER;
    parser->sync_lost++;
}

bool ld2410_parser_parse_frame(const uint8_t *buffer,
                               size_t len,
                               radar_presence_frame_t *frame)
{
    /* Minimum frame size check */
    if (len < LD2410_ENG_FRAME_SIZE) {
        return false;
    }

    /* Validate header */
    if (memcmp(buffer, LD2410_HEADER, LD2410_HEADER_SIZE) != 0) {
        return false;
    }

    /* Validate footer */
    if (memcmp(buffer + OFFSET_FOOTER, LD2410_FOOTER, LD2410_FOOTER_SIZE) != 0) {
        return false;
    }

    /* Validate data type (must be engineering mode) */
    if (buffer[OFFSET_DATA_TYPE] != LD2410_DATA_TYPE_ENG) {
        return false;
    }

    /* Validate frame markers */
    if (buffer[OFFSET_HEAD] != LD2410_FRAME_HEAD ||
        buffer[OFFSET_TAIL] != LD2410_FRAME_TAIL) {
        return false;
    }

    /* Parse target state */
    uint8_t state_byte = buffer[OFFSET_TARGET_STATE];
    if (state_byte > LD2410_MOVING_AND_STATIONARY) {
        frame->state = LD2410_NO_TARGET;
    } else {
        frame->state = (ld2410_target_state_t)state_byte;
    }

    /* Parse distance and energy */
    frame->moving_distance_cm = read_uint16_le(buffer + OFFSET_MOVING_DIST);
    frame->moving_energy = buffer[OFFSET_MOVING_ENERGY];
    frame->stationary_distance_cm = read_uint16_le(buffer + OFFSET_STAT_DIST);
    frame->stationary_energy = buffer[OFFSET_STAT_ENERGY];

    /* Parse gate energies (8 gates from the frame, pad 9th to 0) */
    for (int i = 0; i < 8; i++) {
        frame->moving_gates[i] = buffer[OFFSET_MOVING_GATES + i];
        frame->stationary_gates[i] = buffer[OFFSET_STAT_GATES + i];
    }
    /* 9th gate (some firmware versions) - set to 0 if not present */
    frame->moving_gates[8] = 0;
    frame->stationary_gates[8] = 0;

    /* Set timestamp */
    frame->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    return true;
}

bool ld2410_parser_feed(ld2410_parser_t *parser,
                        const uint8_t *data,
                        size_t len,
                        radar_presence_frame_t *frame)
{
    bool frame_ready = false;

    for (size_t i = 0; i < len && !frame_ready; i++) {
        uint8_t byte = data[i];

        switch (parser->state) {
            case LD2410_STATE_WAIT_HEADER:
                /* Match header sequence byte by byte */
                if (byte == LD2410_HEADER[parser->header_matched]) {
                    parser->buffer[parser->header_matched] = byte;
                    parser->header_matched++;

                    if (parser->header_matched == LD2410_HEADER_SIZE) {
                        /* Header complete, start receiving data */
                        parser->buffer_idx = LD2410_HEADER_SIZE;
                        parser->state = LD2410_STATE_RECEIVE_DATA;
                    }
                } else {
                    /* Mismatch - check if this byte starts a new header */
                    parser->header_matched = 0;
                    if (byte == LD2410_HEADER[0]) {
                        parser->buffer[0] = byte;
                        parser->header_matched = 1;
                    }
                }
                break;

            case LD2410_STATE_RECEIVE_DATA:
                parser->buffer[parser->buffer_idx++] = byte;

                /* Check if we have enough data to read length field */
                if (parser->buffer_idx == 6 && parser->expected_len == 0) {
                    /* Read data length (excludes header, includes type to footer) */
                    parser->expected_len = read_uint16_le(parser->buffer + OFFSET_DATA_LEN);

                    /* Sanity check - engineering mode should be ~29 bytes data */
                    if (parser->expected_len < 20 || parser->expected_len > 50) {
                        ld2410_parser_reset(parser);
                        parser->frames_invalid++;
                        continue;
                    }
                }

                /* Check if frame is complete
                 * Total size = header(4) + len_field(2) + data(expected_len) + footer(4)
                 * But expected_len includes up to tail (0x55 0x00), footer is separate
                 * Typical engineering mode: 39 bytes total
                 */
                if (parser->buffer_idx >= LD2410_ENG_FRAME_SIZE) {
                    /* Try to parse */
                    if (ld2410_parser_parse_frame(parser->buffer,
                                                  parser->buffer_idx,
                                                  frame)) {
                        frame->frame_seq = parser->frame_seq++;
                        parser->frames_parsed++;
                        frame_ready = true;
                    } else {
                        parser->frames_invalid++;
                    }

                    /* Reset for next frame */
                    parser->buffer_idx = 0;
                    parser->header_matched = 0;
                    parser->expected_len = 0;
                    parser->state = LD2410_STATE_WAIT_HEADER;
                }
                break;

            default:
                ld2410_parser_reset(parser);
                break;
        }
    }

    return frame_ready;
}

/* ============================================================================
 * Command Building Functions
 * ============================================================================ */

/**
 * @brief Build a command frame with the standard wrapper
 *
 * Command format:
 * FD FC FB FA [len_lo] [len_hi] [cmd] [data...] 04 03 02 01
 */
static size_t build_command(uint8_t *buffer, uint8_t cmd,
                            const uint8_t *data, size_t data_len)
{
    size_t idx = 0;

    /* Header */
    memcpy(buffer + idx, CMD_HEADER, 4);
    idx += 4;

    /* Length (cmd + data) */
    uint16_t len = 2 + data_len; /* cmd(2 bytes) + data */
    buffer[idx++] = len & 0xFF;
    buffer[idx++] = (len >> 8) & 0xFF;

    /* Command word (2 bytes, little-endian) */
    buffer[idx++] = cmd;
    buffer[idx++] = 0x00;

    /* Data */
    if (data && data_len > 0) {
        memcpy(buffer + idx, data, data_len);
        idx += data_len;
    }

    /* Footer */
    memcpy(buffer + idx, CMD_FOOTER, 4);
    idx += 4;

    return idx;
}

size_t ld2410_build_enable_config(uint8_t *buffer)
{
    /* Enable config: FD FC FB FA 04 00 FF 00 01 00 04 03 02 01 */
    uint8_t data[] = {0x01, 0x00};
    return build_command(buffer, LD2410_CMD_ENABLE_CONFIG, data, sizeof(data));
}

size_t ld2410_build_disable_config(uint8_t *buffer)
{
    /* Disable config: FD FC FB FA 02 00 FE 00 04 03 02 01 */
    return build_command(buffer, LD2410_CMD_DISABLE_CONFIG, NULL, 0);
}

size_t ld2410_build_enable_engineering_mode(uint8_t *buffer)
{
    /* Full sequence to enable engineering mode:
     * 1. Enable config mode
     * 2. Enable engineering output
     * 3. Disable config mode
     *
     * We'll return a single command here - caller should send
     * enable_config first, then this, then disable_config.
     */

    /* Engineering mode enable: FD FC FB FA 02 00 62 00 04 03 02 01 */
    return build_command(buffer, LD2410_CMD_ENABLE_ENG_MODE, NULL, 0);
}

void ld2410_parser_get_stats(const ld2410_parser_t *parser,
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
