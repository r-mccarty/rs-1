/**
 * @file ld2450_parser.h
 * @brief LD2450 24GHz mmWave Radar Frame Parser
 *
 * Parses binary UART frames from the LD2450 radar module into
 * structured target detection data.
 *
 * Frame format (40 bytes):
 * - Header: 0xAA 0xFF 0x03 0x00 (4 bytes)
 * - Target 1: X, Y, Speed, Resolution (8 bytes each × 3 targets = 24 bytes)
 * - Checksum: 2 bytes
 * - Footer: 0x55 0xCC (2 bytes)
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md Section 3.2
 */

#ifndef LD2450_PARSER_H
#define LD2450_PARSER_H

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

#define LD2450_FRAME_SIZE       40
#define LD2450_HEADER_SIZE      4
#define LD2450_FOOTER_SIZE      2
#define LD2450_CHECKSUM_SIZE    2
#define LD2450_TARGET_SIZE      8
#define LD2450_MAX_TARGETS      3

/* Header bytes */
#define LD2450_HEADER_0         0xAA
#define LD2450_HEADER_1         0xFF
#define LD2450_HEADER_2         0x03
#define LD2450_HEADER_3         0x00

/* Footer bytes */
#define LD2450_FOOTER_0         0x55
#define LD2450_FOOTER_1         0xCC

/* Coordinate bounds */
#define LD2450_X_MIN            (-6000)
#define LD2450_X_MAX            6000
#define LD2450_Y_MIN            0
#define LD2450_Y_MAX            6000

/* Invalid/empty target marker */
#define LD2450_INVALID_COORD    0x8000

/* ============================================================================
 * Parser State Machine
 * ============================================================================ */

/**
 * @brief Parser state enumeration
 */
typedef enum {
    LD2450_STATE_WAIT_HEADER,   /**< Waiting for header bytes */
    LD2450_STATE_RECEIVE_DATA,  /**< Receiving frame data */
    LD2450_STATE_COMPLETE       /**< Frame complete, ready for parsing */
} ld2450_parser_state_t;

/**
 * @brief Parser context structure
 */
typedef struct {
    uint8_t buffer[LD2450_FRAME_SIZE];  /**< Frame buffer */
    size_t buffer_idx;                   /**< Current buffer position */
    ld2450_parser_state_t state;         /**< Parser state */
    uint8_t header_matched;              /**< Number of header bytes matched */

    /* Statistics */
    uint32_t frames_parsed;              /**< Successfully parsed frames */
    uint32_t frames_invalid;             /**< Invalid frames (checksum, format) */
    uint32_t sync_lost;                  /**< Sync recovery events */

    /* Frame sequence */
    uint32_t frame_seq;                  /**< Monotonic frame counter */
} ld2450_parser_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize LD2450 parser
 *
 * @param parser Parser context to initialize
 */
void ld2450_parser_init(ld2450_parser_t *parser);

/**
 * @brief Reset parser state (sync recovery)
 *
 * @param parser Parser context
 */
void ld2450_parser_reset(ld2450_parser_t *parser);

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
bool ld2450_parser_feed(ld2450_parser_t *parser,
                        const uint8_t *data,
                        size_t len,
                        radar_detection_frame_t *frame);

/**
 * @brief Parse a complete frame buffer
 *
 * Parses a 40-byte frame buffer directly. Use this when you have
 * buffered a complete frame externally.
 *
 * @param buffer 40-byte frame buffer
 * @param frame Output frame structure
 * @return true if frame is valid, false otherwise
 */
bool ld2450_parser_parse_frame(const uint8_t *buffer,
                               radar_detection_frame_t *frame);

/**
 * @brief Validate frame checksum
 *
 * @param buffer Frame buffer
 * @param len Buffer length
 * @return true if checksum is valid
 */
bool ld2450_parser_validate_checksum(const uint8_t *buffer, size_t len);

/**
 * @brief Get parser statistics
 *
 * @param parser Parser context
 * @param frames_parsed Output: total parsed frames
 * @param frames_invalid Output: total invalid frames
 */
void ld2450_parser_get_stats(const ld2450_parser_t *parser,
                             uint32_t *frames_parsed,
                             uint32_t *frames_invalid);

#ifdef __cplusplus
}
#endif

#endif /* LD2450_PARSER_H */
