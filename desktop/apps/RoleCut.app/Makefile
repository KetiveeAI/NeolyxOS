# RoleCut Video Editor - Makefile
# Copyright (c) 2025 KetiveeAI

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
CFLAGS += -I./include

# Directories
SRC_DIR = src
LIB_DIR = lib
BIN_DIR = bin
OBJ_DIR = obj

# Sources
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Targets
TARGET = $(BIN_DIR)/rolecut
LIBRARY = $(LIB_DIR)/librolecut.so

# Default target
all: dirs $(TARGET)

# Create directories
dirs:
	@mkdir -p $(BIN_DIR) $(LIB_DIR) $(OBJ_DIR) $(LIB_DIR)/codecs

# Build executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Build shared library
library: dirs $(OBJS)
	$(CC) $(CFLAGS) -shared -fPIC -o $(LIBRARY) $(OBJS) -lm

# Compile sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR)

# Install
install: all
	cp $(TARGET) /Applications/RoleCut.app/bin/
	cp $(LIBRARY) /Applications/RoleCut.app/lib/

.PHONY: all dirs library clean install
