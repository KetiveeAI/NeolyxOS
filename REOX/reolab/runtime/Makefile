# REOX Runtime Library Makefile
# Optimized for LTO and inlining
# Enhanced with transitions, color system, lighting, gestures, accessibility

CC = gcc
CFLAGS = -Wall -Wextra -O3 -fPIC -flto -ffunction-sections -fdata-sections
LDFLAGS = -lm -Wl,--gc-sections

# Core source files
CORE_SRC = reox_runtime.c reox_ui.c reox_wrappers.c reox_animation.c reox_theme.c
CORE_OBJ = reox_runtime.o reox_ui.o reox_wrappers.o reox_animation.o reox_theme.o

# Extended modules source files
EXT_SRC = reox_transitions.c reox_color_system.c
EXT_OBJ = reox_transitions.o reox_color_system.o

# All objects
RUNTIME_OBJ = $(CORE_OBJ) $(EXT_OBJ)
RUNTIME_LIB = libreox_runtime.a

# Default target
all: $(RUNTIME_LIB)

# Core module objects
reox_runtime.o: reox_runtime.c reox_runtime.h
	$(CC) $(CFLAGS) -c reox_runtime.c -o reox_runtime.o

reox_ui.o: reox_ui.c reox_ui.h reox_runtime.h
	$(CC) $(CFLAGS) -c reox_ui.c -o reox_ui.o

reox_wrappers.o: reox_wrappers.c reox_ui.h reox_runtime.h
	$(CC) $(CFLAGS) -c reox_wrappers.c -o reox_wrappers.o

reox_animation.o: reox_animation.c reox_animation.h reox_ui.h
	$(CC) $(CFLAGS) -c reox_animation.c -o reox_animation.o

reox_theme.o: reox_theme.c reox_theme.h reox_ui.h
	$(CC) $(CFLAGS) -c reox_theme.c -o reox_theme.o

# Extended module objects
reox_transitions.o: reox_transitions.c reox_transitions.h reox_animation.h reox_ui.h
	$(CC) $(CFLAGS) -c reox_transitions.c -o reox_transitions.o

reox_color_system.o: reox_color_system.c reox_color_system.h reox_ui.h
	$(CC) $(CFLAGS) -c reox_color_system.c -o reox_color_system.o

# Create static library (LTO-compatible)
$(RUNTIME_LIB): $(RUNTIME_OBJ)
	ar rcs $(RUNTIME_LIB) $(RUNTIME_OBJ)

# Install headers
install-headers:
	mkdir -p /usr/local/include/reox
	cp *.h /usr/local/include/reox/

# Clean build artifacts
clean:
	rm -f $(RUNTIME_OBJ) $(RUNTIME_LIB) libreox_runtime.so

.PHONY: all clean install-headers
