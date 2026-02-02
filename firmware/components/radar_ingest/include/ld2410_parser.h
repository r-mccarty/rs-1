/**
 * @file ld2410_parser.h
 * @brief LD2410 24GHz mmWave Radar Frame Parser
 *
 * Parses binary UART frames from the LD2410 radar module (Engineering Mode).
 * Used for both RS-1 Lite (primary) and RS-1 Pro (presence confidence input).
 *
 * Engineering Mode Frame format (39 bytes):
 * - Header: 0xF4 0xF3 0xF2 0xF1 (4 bytes)
 * - Data Length: 2 bytes (little-endian)
 * - Data Type: 0x01 (Engineering mode)
 * - Frame Head: 0xAA
 * - Target data + Gate energies
 * - Frame Tail: 0x55 0x00
 * - Footer: 0xF8 0xF7 0xF6 0xF5 (4 bytes)
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md Section 3.3
 */

#ifndef LD2410_PARSER_H
#define LD2410_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "radar_ingest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Frame Constants
 * ============================================================================ */

#define LD2410_ENG_FRAME_SIZE   39
#define LD2410_HEADER_SIZE      4
#define LD2410_FOOTER_SIZE      4
#define LD2410_NUM_GATES        9

/* Header bytes */
#define LD2410_HEADER_0         0xF4
#define LD2410_HEADER_1         0xF3
#define LD2410_HEADER_2         0xF2
#define LD2410_HEADER_3         0xF1

/* Footer bytes */
#define LD2410_FOOTER_0         0xF8
#define LD2410_FOOTER_1         0xF7
#define LD2410_FOOTER_2         0xF6
#define LD2410_FOOTER_3         0xF5

/* Data type for engineering mode */
#define LD2410_DATA_TYPE_ENG    0x01

/* Frame markers */
#define LD2410_FRAME_HEAD       0xAA
#define LD2410_FRAME_TAIL       0x55

/* ============================================================================
 * Parser State Machine
 * ============================================================================ */

/**
 * @brief Parser state enumeration
 */
typedef enum {
    LD2410_STATE_WAIT_HEADER,   /**< Waiting for header bytes */
    LD2410_STATE_RECEIVE_DATA,  /**< Receiving frame data */
    LD2410_STATE_COMPLETE       /**< Frame complete, ready for parsing */
} ld2410_parser_state_t;

/**
 * @brief Parser context structure
 */
typedef struct {
    uint8_t buffer[LD2410_ENG_FRAME_SIZE + 16]; /**< Frame buffer with margin */
    size_t buffer_idx;                   /**< Current buffer position */
    ld2410_parser_state_t state;         /**< Parser state */
    uint8_t header_matched;              /**< Number of header bytes matched */
    uint16_t expected_len;               /**< Expected data length from frame */

    /* Statistics */
    uint32_t frames_parsed;              /**< Successfully parsed frames */
    uint32_t frames_invalid;             /**< Invalid frames */
    uint32_t sync_lost;                  /**< Sync recovery events */

    /* Frame sequence */
    uint32_t frame_seq;                  /**< Monotonic frame counter */
} ld2410_parser_t;

/* ============================================================================
 * Command Structures (for radar configuration)
 * ============================================================================ */

/**
 * @brief LD2410 command IDs
 */
typedef enum {
    LD2410_CMD_ENABLE_CONFIG    = 0xFF,
    LD2410_CMD_DISABLE_CONFIG   = 0xFE,
    LD2410_CMD_ENABLE_ENG_MODE  = 0x62,
    LD2410_CMD_DISABLE_ENG_MODE = 0x63,
    LD2410_CMD_SET_MAX_GATE     = 0x60,
    LD2410_CMD_READ_FIRMWARE    = 0xA0,
    LD2410_CMD_RESTART          = 0xA3,
} ld2410_cmd_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize LD2410 parser
 *
 * @param parser Parser context to initialize
 */
void ld2410_parser_init(ld2410_parser_t *parser);

/**
 * @brief Reset parser state (sync recovery)
 *
 * @param parser Parser context
 */
void ld2410_parser_reset(ld2410_parser_t *parser);

/**
 * @brief Feed bytes to the parser
 *
 * Process incoming UART bytes through the parser state machine.
 * When a complete valid frame is detected, returns true and
 * populates the output frame structure.
 *
 * @param parser Parser context
 * @param data Input byte array
 * @param len Number of bytes to process
 * @param frame Output frame (populated when function returns true)
 * @return true if a complete frame was parsed, false otherwise
 */
bool ld2410_parser_feed(ld2410_parser_t *parser,
                        const uint8_t *data,
                        size_t len,
                        radar_presence_frame_t *frame);

/**
 * @brief Parse a complete engineering mode frame buffer
 *
 * @param buffer Frame buffer (minimum 39 bytes)
 * @param len Buffer length
 * @param frame Output frame structure
 * @return true if frame is valid, false otherwise
 */
bool ld2410_parser_parse_frame(const uint8_t *buffer,
                               size_t len,
                               radar_presence_frame_t *frame);

/**
 * @brief Build command to enable engineering mode
 *
 * @param buffer Output buffer (minimum 18 bytes)
 * @return Command length
 */
size_t ld2410_build_enable_engineering_mode(uint8_t *buffer);

/**
 * @brief Build command to enable configuration mode
 *
 * @param buffer Output buffer (minimum 14 bytes)
 * @return Command length
 */
size_t ld2410_build_enable_config(uint8_t *buffer);

/**
 * @brief Build command to disable configuration mode
 *
 * @param buffer Output buffer (minimum 14 bytes)
 * @return Command length
 */
size_t ld2410_build_disable_config(uint8_t *buffer);

/**
 * @brief Get parser statistics
 *
 * @param parser Parser context
 * @param frames_parsed Output: total parsed frames
 * @param frames_invalid Output: total invalid frames
 */
void ld2410_parser_get_stats(const ld2410_parser_t *parser,
                             uint32_t *frames_parsed,
                             uint32_t *frames_invalid);

#ifdef __cplusplus
}
#endif

#endif /* LD2410_PARSER_H */
