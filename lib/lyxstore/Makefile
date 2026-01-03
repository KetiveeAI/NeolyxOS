# LyxStore Marketplace
# Makefile for NeolyxOS

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -fPIC
LDFLAGS = -shared

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
LIB_DIR = lib

# Target
LIB_NAME = liblyxstore.so
STATIC_LIB = liblyxstore.a

# Sources
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(SRCS)))

.PHONY: all clean install

all: $(BUILD_DIR) $(LIB_DIR)/$(LIB_NAME) $(LIB_DIR)/$(STATIC_LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(LIB_DIR)

# Shared library
$(LIB_DIR)/$(LIB_NAME): $(SRC_DIR)/store_client.c
	$(CC) $(CFLAGS) $(LDFLAGS) -I$(INC_DIR) $^ -o $@

# Static library
$(LIB_DIR)/$(STATIC_LIB): $(BUILD_DIR)/store_client.o
	ar rcs $@ $^

$(BUILD_DIR)/store_client.o: $(SRC_DIR)/store_client.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# Install to system
install: all
	cp $(LIB_DIR)/$(LIB_NAME) /usr/local/lib/
	cp $(LIB_DIR)/$(STATIC_LIB) /usr/local/lib/
	mkdir -p /usr/local/include/lyxstore
	cp $(INC_DIR)/*.h /usr/local/include/lyxstore/
	ldconfig

clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR)
