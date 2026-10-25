# Calculator.app Makefile
# NeolyxOS Calculator

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -fPIC
LDFLAGS = -lm

# NXRender integration
NXRENDER_DIR = ../../gui/nxrender_c
NXRENDER_INCLUDE = -I$(NXRENDER_DIR)/include
NXRENDER_LIB = -L$(NXRENDER_DIR)/lib -lnxrender

# NXAudio integration
NXAUDIO_DIR = ../../nxaudio
NXAUDIO_INCLUDE = -I$(NXAUDIO_DIR)/include
NXAUDIO_LIB = -L$(NXAUDIO_DIR)/build -lnxaudio

# Source files
SRCS = main.c display.c buttons.c engine.c history.c scientific.c nxaudio_feedback.c
OBJS = $(SRCS:.c=.o)

# Output directories
BIN_DIR = bin
LIB_DIR = lib

# Targets
BIN_TARGET = $(BIN_DIR)/calculator
LIB_TARGET = $(LIB_DIR)/libcalculator.so

# Build modes
.PHONY: all clean install standalone real dirs

all: standalone

dirs:
	@mkdir -p $(BIN_DIR) $(LIB_DIR)

# Standalone (no NXRender/NXAudio, uses stubs)
standalone: CFLAGS += -DSTANDALONE
standalone: dirs $(BIN_TARGET)

# Real NXRender + NXAudio build
real: CFLAGS += -DNXRENDER_REAL -DNXAUDIO_REAL $(NXRENDER_INCLUDE) $(NXAUDIO_INCLUDE)
real: LDFLAGS += $(NXRENDER_LIB) $(NXAUDIO_LIB)
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
	mkdir -p /Applications/Calculator.app/bin
	mkdir -p /Applications/Calculator.app/lib
	mkdir -p /Applications/Calculator.app/resources
	cp $(BIN_TARGET) /Applications/Calculator.app/bin/
	cp -r resources/* /Applications/Calculator.app/resources/
	cp manifest.npa /Applications/Calculator.app/
	@echo "Installed Calculator.app"

# Dependencies
main.o: main.c nxrender.h calc.h nxaudio_feedback.h
display.o: display.c nxrender.h calc.h
buttons.o: buttons.c nxrender.h calc.h
engine.o: engine.c calc.h
history.o: history.c calc.h
scientific.o: scientific.c calc.h
nxaudio_feedback.o: nxaudio_feedback.c nxaudio_feedback.h
