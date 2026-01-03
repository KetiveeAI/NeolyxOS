# Terminal.app Makefile
# NeolyxOS Terminal Emulator

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -fPIC
LDFLAGS = 

# NXRender integration
NXRENDER_DIR = ../../gui/nxrender_c
NXRENDER_INCLUDE = -I$(NXRENDER_DIR)/include
NXRENDER_LIB = -L$(NXRENDER_DIR)/lib -lnxrender

# Source files
SRCS = main.c terminal_view.c context_menu.c
OBJS = $(SRCS:.c=.o)

# Output directories
BIN_DIR = bin
LIB_DIR = lib

# Targets
BIN_TARGET = $(BIN_DIR)/terminal
LIB_TARGET = $(LIB_DIR)/libterminal.so

# Build modes
.PHONY: all clean install standalone real dirs

all: standalone

dirs:
	@mkdir -p $(BIN_DIR) $(LIB_DIR)

# Standalone (no NXRender, uses stubs)
standalone: CFLAGS += -DSTANDALONE
standalone: dirs $(BIN_TARGET)

# Real NXRender build
real: CFLAGS += -DNXRENDER_REAL $(NXRENDER_INCLUDE)
real: LDFLAGS += $(NXRENDER_LIB)
real: dirs $(BIN_TARGET) $(LIB_TARGET)

$(BIN_TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(BIN_TARGET) $(LDFLAGS)

$(LIB_TARGET): $(OBJS)
	$(CC) -shared $(OBJS) -o $(LIB_TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
	rm -f $(BIN_TARGET) $(LIB_TARGET)

install: $(BIN_TARGET)
	mkdir -p /Applications/Terminal.app/bin
	mkdir -p /Applications/Terminal.app/lib
	mkdir -p /Applications/Terminal.app/resources
	cp $(BIN_TARGET) /Applications/Terminal.app/bin/
	cp -r resources/* /Applications/Terminal.app/resources/
	cp manifest.npa /Applications/Terminal.app/
	@echo "Installed Terminal.app"

# Dependencies
main.o: main.c nxrender.h
terminal_view.o: terminal_view.c nxrender.h
context_menu.o: context_menu.c nxrender.h
