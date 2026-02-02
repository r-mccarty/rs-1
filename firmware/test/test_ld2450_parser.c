/**
 * @file test_ld2450_parser.c
 * @brief Unit Tests for LD2450 Parser
 *
 * Tests the LD2450 frame parser with known-good and malformed frames.
 *
 * Build: gcc -o test_ld2450 test_ld2450_parser.c ../components/radar_ingest/ld2450_parser.c -I../components/radar_ingest/include -DTEST_HOST
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/* Mock esp_timer for host testing */
#ifdef TEST_HOST
static uint64_t mock_time_us = 0;
uint64_t esp_timer_get_time(void) { return mock_time_us; }
void mock_set_time(uint64_t us) { mock_time_us = us; }
#endif

#include "ld2450_parser.h"

/* Test utilities */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  TEST: %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) { printf("Assertion failed: %s\n", #x); return false; } } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { printf("Expected %d, got %d\n", (int)(b), (int)(a)); return false; } } while(0)

/* ============================================================================
 * Test Data: Known-Good LD2450 Frames
 * ============================================================================ */

/* Frame with 1 target at (1000, 2000, 50cm/s) */
static const uint8_t frame_one_target[] = {
    0xAA, 0xFF, 0x03, 0x00,  /* Header */
    /* Target 1: X=1000, Y=2000, Speed=50, Res=100 */
    0xE8, 0x03,              /* X = 1000 (little-endian) */
    0xD0, 0x07,              /* Y = 2000 */
    0x32, 0x00,              /* Speed = 50 */
    0x64, 0x00,              /* Resolution = 100 */
    /* Target 2: Invalid */
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    /* Target 3: Invalid */
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    /* Checksum (placeholder - accept 0x0000) */
    0x00, 0x00,
    /* Footer */
    0x55, 0xCC
};

/* Frame with 3 targets */
static const uint8_t frame_three_targets[] = {
    0xAA, 0xFF, 0x03, 0x00,  /* Header */
    /* Target 1: X=500, Y=1500, Speed=-30 (approaching), Res=80 */
    0xF4, 0x01, 0xDC, 0x05, 0xE2, 0xFF, 0x50, 0x00,
    /* Target 2: X=-300, Y=2500, Speed=100, Res=150 */
    0xD4, 0xFE, 0xC4, 0x09, 0x64, 0x00, 0x96, 0x00,
    /* Target 3: X=1200, Y=3000, Speed=0, Res=200 */
    0xB0, 0x04, 0xB8, 0x0B, 0x00, 0x00, 0xC8, 0x00,
    /* Checksum (0x0000 = accept) */
    0x00, 0x00,
    /* Footer */
    0x55, 0xCC
};

/* Frame with no targets (all invalid) */
static const uint8_t frame_no_targets[] = {
    0xAA, 0xFF, 0x03, 0x00,  /* Header */
    /* All targets invalid (0x8000 marker) */
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    /* Checksum */
    0x00, 0x00,
    /* Footer */
    0x55, 0xCC
};

/* Malformed frame - bad header */
static const uint8_t frame_bad_header[] = {
    0xAA, 0xFF, 0x04, 0x00,  /* Wrong header byte */
    0xE8, 0x03, 0xD0, 0x07, 0x32, 0x00, 0x64, 0x00,
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x55, 0xCC
};

/* Malformed frame - bad footer */
static const uint8_t frame_bad_footer[] = {
    0xAA, 0xFF, 0x03, 0x00,
    0xE8, 0x03, 0xD0, 0x07, 0x32, 0x00, 0x64, 0x00,
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x55, 0xCD  /* Wrong footer byte */
};

/* ============================================================================
 * Tests
 * ============================================================================ */

bool test_parser_init(void) {
    ld2450_parser_t parser;
    ld2450_parser_init(&parser);

    ASSERT_EQ(parser.state, LD2450_STATE_WAIT_HEADER);
    ASSERT_EQ(parser.buffer_idx, 0);
    ASSERT_EQ(parser.header_matched, 0);
    ASSERT_EQ(parser.frames_parsed, 0);
    ASSERT_EQ(parser.frames_invalid, 0);

    return true;
}

bool test_parse_one_target(void) {
    radar_detection_frame_t frame;
    bool result = ld2450_parser_parse_frame(frame_one_target, &frame);

    ASSERT_TRUE(result);
    ASSERT_EQ(frame.target_count, 1);

    /* Check target 1 */
    ASSERT_TRUE(frame.targets[0].valid);
    ASSERT_EQ(frame.targets[0].x_mm, 1000);
    ASSERT_EQ(frame.targets[0].y_mm, 2000);
    ASSERT_EQ(frame.targets[0].speed_cm_s, 50);
    ASSERT_EQ(frame.targets[0].resolution_mm, 100);

    /* Check targets 2 and 3 are invalid */
    ASSERT_FALSE(frame.targets[1].valid);
    ASSERT_FALSE(frame.targets[2].valid);

    return true;
}

bool test_parse_three_targets(void) {
    radar_detection_frame_t frame;
    bool result = ld2450_parser_parse_frame(frame_three_targets, &frame);

    ASSERT_TRUE(result);
    ASSERT_EQ(frame.target_count, 3);

    /* Target 1: X=500, Y=1500, Speed=-30 */
    ASSERT_TRUE(frame.targets[0].valid);
    ASSERT_EQ(frame.targets[0].x_mm, 500);
    ASSERT_EQ(frame.targets[0].y_mm, 1500);
    ASSERT_EQ(frame.targets[0].speed_cm_s, -30);

    /* Target 2: X=-300, Y=2500, Speed=100 */
    ASSERT_TRUE(frame.targets[1].valid);
    ASSERT_EQ(frame.targets[1].x_mm, -300);
    ASSERT_EQ(frame.targets[1].y_mm, 2500);
    ASSERT_EQ(frame.targets[1].speed_cm_s, 100);

    /* Target 3: X=1200, Y=3000, Speed=0 */
    ASSERT_TRUE(frame.targets[2].valid);
    ASSERT_EQ(frame.targets[2].x_mm, 1200);
    ASSERT_EQ(frame.targets[2].y_mm, 3000);
    ASSERT_EQ(frame.targets[2].speed_cm_s, 0);

    return true;
}

bool test_parse_no_targets(void) {
    radar_detection_frame_t frame;
    bool result = ld2450_parser_parse_frame(frame_no_targets, &frame);

    ASSERT_TRUE(result);
    ASSERT_EQ(frame.target_count, 0);
    ASSERT_FALSE(frame.targets[0].valid);
    ASSERT_FALSE(frame.targets[1].valid);
    ASSERT_FALSE(frame.targets[2].valid);

    return true;
}

bool test_reject_bad_header(void) {
    radar_detection_frame_t frame;
    bool result = ld2450_parser_parse_frame(frame_bad_header, &frame);

    ASSERT_FALSE(result);
    return true;
}

bool test_reject_bad_footer(void) {
    radar_detection_frame_t frame;
    bool result = ld2450_parser_parse_frame(frame_bad_footer, &frame);

    ASSERT_FALSE(result);
    return true;
}

bool test_streaming_parser(void) {
    ld2450_parser_t parser;
    radar_detection_frame_t frame;

    ld2450_parser_init(&parser);

    /* Feed frame byte by byte */
    bool found = false;
    for (size_t i = 0; i < sizeof(frame_one_target); i++) {
        if (ld2450_parser_feed(&parser, &frame_one_target[i], 1, &frame)) {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);
    ASSERT_EQ(frame.target_count, 1);
    ASSERT_EQ(frame.targets[0].x_mm, 1000);

    return true;
}

bool test_streaming_parser_with_garbage(void) {
    ld2450_parser_t parser;
    radar_detection_frame_t frame;

    ld2450_parser_init(&parser);

    /* Feed garbage before valid frame */
    uint8_t garbage[] = {0x12, 0x34, 0x56, 0xAA, 0x00, 0xFF};
    ld2450_parser_feed(&parser, garbage, sizeof(garbage), &frame);

    /* Now feed valid frame */
    bool found = ld2450_parser_feed(&parser, frame_one_target,
                                     sizeof(frame_one_target), &frame);

    ASSERT_TRUE(found);
    ASSERT_EQ(frame.target_count, 1);

    return true;
}

bool test_streaming_multiple_frames(void) {
    ld2450_parser_t parser;
    radar_detection_frame_t frame;

    ld2450_parser_init(&parser);

    /* Feed first frame */
    bool found1 = ld2450_parser_feed(&parser, frame_one_target,
                                      sizeof(frame_one_target), &frame);
    ASSERT_TRUE(found1);
    ASSERT_EQ(frame.frame_seq, 0);

    /* Feed second frame */
    bool found2 = ld2450_parser_feed(&parser, frame_three_targets,
                                      sizeof(frame_three_targets), &frame);
    ASSERT_TRUE(found2);
    ASSERT_EQ(frame.frame_seq, 1);
    ASSERT_EQ(frame.target_count, 3);

    return true;
}

bool test_parser_stats(void) {
    ld2450_parser_t parser;
    radar_detection_frame_t frame;

    ld2450_parser_init(&parser);

    /* Feed valid frame */
    ld2450_parser_feed(&parser, frame_one_target, sizeof(frame_one_target), &frame);

    /* Feed invalid frame */
    ld2450_parser_feed(&parser, frame_bad_header, sizeof(frame_bad_header), &frame);

    /* Feed another valid frame */
    ld2450_parser_feed(&parser, frame_three_targets, sizeof(frame_three_targets), &frame);

    uint32_t parsed, invalid;
    ld2450_parser_get_stats(&parser, &parsed, &invalid);

    ASSERT_EQ(parsed, 2);
    ASSERT_EQ(invalid, 1);

    return true;
}

bool test_signal_quality_calculation(void) {
    radar_detection_frame_t frame;
    ld2450_parser_parse_frame(frame_one_target, &frame);

    /* Resolution 100mm should give ~89 quality */
    /* Quality = 100 - (res - 100) * 100 / 900 = 100 - 0 = 100 for res=100 */
    ASSERT_TRUE(frame.targets[0].signal_quality >= 90);

    return true;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("LD2450 Parser Unit Tests\n");
    printf("========================\n\n");

    #ifdef TEST_HOST
    mock_set_time(1000000); /* 1 second */
    #endif

    TEST(parser_init);
    TEST(parse_one_target);
    TEST(parse_three_targets);
    TEST(parse_no_targets);
    TEST(reject_bad_header);
    TEST(reject_bad_footer);
    TEST(streaming_parser);
    TEST(streaming_parser_with_garbage);
    TEST(streaming_multiple_frames);
    TEST(parser_stats);
    TEST(signal_quality_calculation);

    printf("\n========================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
