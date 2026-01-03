# ZebraScript Shared Engine
# Makefile for NeolyxOS

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -fPIC
LDFLAGS = -shared

# Directories
SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
BUILD_DIR = build
LIB_DIR = lib

# Target
LIB_NAME = libzeprascript.so
STATIC_LIB = libzeprascript.a
TEST_BIN = test_zeprascript

# Sources
SRCS = $(wildcard $(SRC_DIR)/*.c) \
       $(wildcard $(SRC_DIR)/runtime/*.c) \
       $(wildcard $(SRC_DIR)/builtins/*.c) \
       $(wildcard $(SRC_DIR)/bytecode/*.c) \
       $(wildcard $(SRC_DIR)/jit/*.c) \
       $(wildcard $(SRC_DIR)/gc/*.c) \
       $(wildcard $(SRC_DIR)/frontend/*.c)

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(SRCS)))

# Test sources
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)

.PHONY: all clean test install

all: $(BUILD_DIR) $(LIB_DIR)/$(LIB_NAME) $(LIB_DIR)/$(STATIC_LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(LIB_DIR)

# Shared library
$(LIB_DIR)/$(LIB_NAME): $(SRC_DIR)/engine.c
	$(CC) $(CFLAGS) $(LDFLAGS) -I$(INC_DIR) $^ -o $@

# Static library
$(LIB_DIR)/$(STATIC_LIB): $(BUILD_DIR)/engine.o
	ar rcs $@ $^

$(BUILD_DIR)/engine.o: $(SRC_DIR)/engine.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# Tests
test: $(BUILD_DIR)/$(TEST_BIN)
	./$(BUILD_DIR)/$(TEST_BIN)

$(BUILD_DIR)/$(TEST_BIN): $(TEST_DIR)/basic_tests.c $(SRC_DIR)/engine.c
	$(CC) $(CFLAGS) -I$(INC_DIR) $^ -o $@

# Install to system
install: all
	cp $(LIB_DIR)/$(LIB_NAME) /usr/local/lib/
	cp $(LIB_DIR)/$(STATIC_LIB) /usr/local/lib/
	mkdir -p /usr/local/include/zeprascript
	cp $(INC_DIR)/*.h /usr/local/include/zeprascript/
	ldconfig

clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR)

# Auto-sync from ZepraBrowser
sync:
	@echo "Syncing from ZepraBrowser source..."
	@# TODO: Copy updated files from zeprabrowser/source/zepraScript
