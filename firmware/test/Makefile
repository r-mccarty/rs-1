# RS-1 Firmware Test Makefile
# Builds and runs unit tests on host (not ESP32)

CC = gcc
CFLAGS = -Wall -Wextra -g -DTEST_HOST
INCLUDES = -I../components/radar_ingest/include

# Source directories
RADAR_SRC = ../components/radar_ingest

# Test executables
TESTS = test_ld2450 test_ld2410

.PHONY: all clean test

all: $(TESTS)

test_ld2450: test_ld2450_parser.c $(RADAR_SRC)/ld2450_parser.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test_ld2410: test_ld2410_parser.c $(RADAR_SRC)/ld2410_parser.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test: $(TESTS)
	@echo "================================"
	@echo "Running LD2450 Parser Tests"
	@echo "================================"
	./test_ld2450
	@echo ""
	@echo "================================"
	@echo "Running LD2410 Parser Tests"
	@echo "================================"
	./test_ld2410

clean:
	rm -f $(TESTS)
