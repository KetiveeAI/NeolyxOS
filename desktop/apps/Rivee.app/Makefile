# Rivee - Draw your imagination
# NeolyxOS Vector Graphics Editor
# Copyright (c) 2025 KetiveeAI

APP_NAME = rivee
VERSION = 1.0.0

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -I../../include -Iinclude
LDFLAGS = -lreoxui -lnxrender -lm

SRC_DIR = src
BIN_DIR = bin
LIB_DIR = lib

SOURCES = main.c \
          $(SRC_DIR)/document/document.c \
          $(SRC_DIR)/document/layer.c \
          $(SRC_DIR)/document/shapes.c \
          $(SRC_DIR)/tools/select.c \
          $(SRC_DIR)/tools/rectangle.c \
          $(SRC_DIR)/tools/ellipse.c \
          $(SRC_DIR)/tools/line.c \
          $(SRC_DIR)/tools/pen.c \
          $(SRC_DIR)/tools/text.c \
          $(SRC_DIR)/panels/toolbar.c \
          $(SRC_DIR)/panels/layers.c \
          $(SRC_DIR)/panels/properties.c \
          $(SRC_DIR)/panels/color.c \
          $(SRC_DIR)/export/nxi_export.c \
          $(SRC_DIR)/export/png_export.c

OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean install

all: $(BIN_DIR)/$(APP_NAME)

$(BIN_DIR)/$(APP_NAME): $(OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "Built Rivee version $(VERSION)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(BIN_DIR)/$(APP_NAME)
	@echo "Cleaned Rivee build files"

install: all
	@echo "Installing Rivee to /System/Applications/"
	cp -r . /System/Applications/Rivee.app/
