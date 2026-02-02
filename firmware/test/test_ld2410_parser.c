/**
 * @file test_ld2410_parser.c
 * @brief Unit Tests for LD2410 Parser
 *
 * Tests the LD2410 Engineering Mode frame parser.
 *
 * Build: gcc -o test_ld2410 test_ld2410_parser.c ../components/radar_ingest/ld2410_parser.c -I../components/radar_ingest/include -DTEST_HOST
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

#include "ld2410_parser.h"

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
 * Test Data: Known-Good LD2410 Engineering Mode Frames
 * ============================================================================ */

/* Engineering mode frame with moving target */
static const uint8_t frame_moving_target[] = {
    0xF4, 0xF3, 0xF2, 0xF1,  /* Header */
    0x1D, 0x00,              /* Data length = 29 */
    0x01,                    /* Data type = Engineering mode */
    0xAA,                    /* Head */
    0x01,                    /* Target state = MOVING */
    0xC8, 0x00,              /* Moving distance = 200cm */
    0x50,                    /* Moving energy = 80 */
    0x00, 0x00,              /* Stationary distance = 0 */
    0x00,                    /* Stationary energy = 0 */
    0xE8, 0x03,              /* Detection distance = 1000cm */
    /* Moving gates 0-7 */
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    /* Stationary gates 0-7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x55,                    /* Tail */
    0x00,                    /* Check */
    0xF8, 0xF7, 0xF6, 0xF5   /* Footer */
};

/* Engineering mode frame with stationary target */
static const uint8_t frame_stationary_target[] = {
    0xF4, 0xF3, 0xF2, 0xF1,  /* Header */
    0x1D, 0x00,              /* Data length = 29 */
    0x01,                    /* Data type = Engineering mode */
    0xAA,                    /* Head */
    0x02,                    /* Target state = STATIONARY */
    0x00, 0x00,              /* Moving distance = 0 */
    0x00,                    /* Moving energy = 0 */
    0x96, 0x00,              /* Stationary distance = 150cm */
    0x3C,                    /* Stationary energy = 60 */
    0x58, 0x02,              /* Detection distance = 600cm */
    /* Moving gates 0-7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Stationary gates 0-7 */
    0x05, 0x10, 0x20, 0x30, 0x3C, 0x28, 0x14, 0x0A,
    0x55,                    /* Tail */
    0x00,                    /* Check */
    0xF8, 0xF7, 0xF6, 0xF5   /* Footer */
};

/* Engineering mode frame with both moving and stationary */
static const uint8_t frame_both_targets[] = {
    0xF4, 0xF3, 0xF2, 0xF1,  /* Header */
    0x1D, 0x00,              /* Data length = 29 */
    0x01,                    /* Data type = Engineering mode */
    0xAA,                    /* Head */
    0x03,                    /* Target state = MOVING_AND_STATIONARY */
    0x64, 0x00,              /* Moving distance = 100cm */
    0x46,                    /* Moving energy = 70 */
    0x2C, 0x01,              /* Stationary distance = 300cm */
    0x32,                    /* Stationary energy = 50 */
    0xDC, 0x05,              /* Detection distance = 1500cm */
    /* Moving gates 0-7 */
    0x46, 0x3C, 0x32, 0x28, 0x1E, 0x14, 0x0A, 0x05,
    /* Stationary gates 0-7 */
    0x00, 0x14, 0x28, 0x32, 0x28, 0x14, 0x0A, 0x05,
    0x55,                    /* Tail */
    0x00,                    /* Check */
    0xF8, 0xF7, 0xF6, 0xF5   /* Footer */
};

/* Engineering mode frame with no target */
static const uint8_t frame_no_target[] = {
    0xF4, 0xF3, 0xF2, 0xF1,  /* Header */
    0x1D, 0x00,              /* Data length = 29 */
    0x01,                    /* Data type = Engineering mode */
    0xAA,                    /* Head */
    0x00,                    /* Target state = NO_TARGET */
    0x00, 0x00,              /* Moving distance = 0 */
    0x00,                    /* Moving energy = 0 */
    0x00, 0x00,              /* Stationary distance = 0 */
    0x00,                    /* Stationary energy = 0 */
    0x00, 0x00,              /* Detection distance = 0 */
    /* All gates zero */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x55,                    /* Tail */
    0x00,                    /* Check */
    0xF8, 0xF7, 0xF6, 0xF5   /* Footer */
};

/* Malformed frame - bad header */
static const uint8_t frame_bad_header[] = {
    0xF4, 0xF3, 0xF2, 0xF0,  /* Wrong header byte */
    0x1D, 0x00, 0x01, 0xAA,
    0x01, 0xC8, 0x00, 0x50, 0x00, 0x00, 0x00, 0xE8, 0x03,
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x55, 0x00,
    0xF8, 0xF7, 0xF6, 0xF5
};

/* ============================================================================
 * Tests
 * ============================================================================ */

bool test_parser_init(void) {
    ld2410_parser_t parser;
    ld2410_parser_init(&parser);

    ASSERT_EQ(parser.state, LD2410_STATE_WAIT_HEADER);
    ASSERT_EQ(parser.buffer_idx, 0);
    ASSERT_EQ(parser.frames_parsed, 0);

    return true;
}

bool test_parse_moving_target(void) {
    radar_presence_frame_t frame;
    bool result = ld2410_parser_parse_frame(frame_moving_target,
                                             sizeof(frame_moving_target),
                                             &frame);

    ASSERT_TRUE(result);
    ASSERT_EQ(frame.state, LD2410_MOVING);
    ASSERT_EQ(frame.moving_distance_cm, 200);
    ASSERT_EQ(frame.moving_energy, 80);
    ASSERT_EQ(frame.stationary_distance_cm, 0);
    ASSERT_EQ(frame.stationary_energy, 0);

    /* Check gate energies */
    ASSERT_EQ(frame.moving_gates[0], 0x10);
    ASSERT_EQ(frame.moving_gates[7], 0x80);
    ASSERT_EQ(frame.stationary_gates[0], 0);

    return true;
}

bool test_parse_stationary_target(void) {
    radar_presence_frame_t frame;
    bool result = ld2410_parser_parse_frame(frame_stationary_target,
                                             sizeof(frame_stationary_target),
                                             &frame);

    ASSERT_TRUE(result);
    ASSERT_EQ(frame.state, LD2410_STATIONARY);
    ASSERT_EQ(frame.moving_distance_cm, 0);
    ASSERT_EQ(frame.moving_energy, 0);
    ASSERT_EQ(frame.stationary_distance_cm, 150);
    ASSERT_EQ(frame.stationary_energy, 60);

    return true;
}

bool test_parse_both_targets(void) {
    radar_presence_frame_t frame;
    bool result = ld2410_parser_parse_frame(frame_both_targets,
                                             sizeof(frame_both_targets),
                                             &frame);

    ASSERT_TRUE(result);
    ASSERT_EQ(frame.state, LD2410_MOVING_AND_STATIONARY);
    ASSERT_EQ(frame.moving_distance_cm, 100);
    ASSERT_EQ(frame.moving_energy, 70);
    ASSERT_EQ(frame.stationary_distance_cm, 300);
    ASSERT_EQ(frame.stationary_energy, 50);

    return true;
}

bool test_parse_no_target(void) {
    radar_presence_frame_t frame;
    bool result = ld2410_parser_parse_frame(frame_no_target,
                                             sizeof(frame_no_target),
                                             &frame);

    ASSERT_TRUE(result);
    ASSERT_EQ(frame.state, LD2410_NO_TARGET);
    ASSERT_EQ(frame.moving_distance_cm, 0);
    ASSERT_EQ(frame.moving_energy, 0);
    ASSERT_EQ(frame.stationary_distance_cm, 0);
    ASSERT_EQ(frame.stationary_energy, 0);

    return true;
}

bool test_reject_bad_header(void) {
    radar_presence_frame_t frame;
    bool result = ld2410_parser_parse_frame(frame_bad_header,
                                             sizeof(frame_bad_header),
                                             &frame);

    ASSERT_FALSE(result);
    return true;
}

bool test_streaming_parser(void) {
    ld2410_parser_t parser;
    radar_presence_frame_t frame;

    ld2410_parser_init(&parser);

    /* Feed frame byte by byte */
    bool found = false;
    for (size_t i = 0; i < sizeof(frame_moving_target); i++) {
        if (ld2410_parser_feed(&parser, &frame_moving_target[i], 1, &frame)) {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);
    ASSERT_EQ(frame.state, LD2410_MOVING);
    ASSERT_EQ(frame.moving_distance_cm, 200);

    return true;
}

bool test_streaming_multiple_frames(void) {
    ld2410_parser_t parser;
    radar_presence_frame_t frame;

    ld2410_parser_init(&parser);

    /* Feed first frame */
    bool found1 = ld2410_parser_feed(&parser, frame_moving_target,
                                      sizeof(frame_moving_target), &frame);
    ASSERT_TRUE(found1);
    ASSERT_EQ(frame.state, LD2410_MOVING);
    ASSERT_EQ(frame.frame_seq, 0);

    /* Feed second frame */
    bool found2 = ld2410_parser_feed(&parser, frame_stationary_target,
                                      sizeof(frame_stationary_target), &frame);
    ASSERT_TRUE(found2);
    ASSERT_EQ(frame.state, LD2410_STATIONARY);
    ASSERT_EQ(frame.frame_seq, 1);

    return true;
}

bool test_build_enable_config_command(void) {
    uint8_t buffer[20];
    size_t len = ld2410_build_enable_config(buffer);

    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(len <= 20);

    /* Check header */
    ASSERT_EQ(buffer[0], 0xFD);
    ASSERT_EQ(buffer[1], 0xFC);
    ASSERT_EQ(buffer[2], 0xFB);
    ASSERT_EQ(buffer[3], 0xFA);

    /* Check footer */
    ASSERT_EQ(buffer[len - 4], 0x04);
    ASSERT_EQ(buffer[len - 3], 0x03);
    ASSERT_EQ(buffer[len - 2], 0x02);
    ASSERT_EQ(buffer[len - 1], 0x01);

    return true;
}

bool test_build_engineering_mode_command(void) {
    uint8_t buffer[20];
    size_t len = ld2410_build_enable_engineering_mode(buffer);

    ASSERT_TRUE(len > 0);

    /* Check command byte (0x62 = enable engineering mode) */
    ASSERT_EQ(buffer[6], 0x62);

    return true;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("LD2410 Parser Unit Tests\n");
    printf("========================\n\n");

    #ifdef TEST_HOST
    mock_set_time(1000000); /* 1 second */
    #endif

    TEST(parser_init);
    TEST(parse_moving_target);
    TEST(parse_stationary_target);
    TEST(parse_both_targets);
    TEST(parse_no_target);
    TEST(reject_bad_header);
    TEST(streaming_parser);
    TEST(streaming_multiple_frames);
    TEST(build_enable_config_command);
    TEST(build_engineering_mode_command);

    printf("\n========================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
