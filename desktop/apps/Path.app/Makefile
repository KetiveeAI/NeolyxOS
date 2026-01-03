# Path.app Makefile
# NeolyxOS File Manager

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -fPIC
LDFLAGS = 

# NXRender integration
NXRENDER_DIR = ../../gui/nxrender_c
NXRENDER_INCLUDE = -I$(NXRENDER_DIR)/include
NXRENDER_LIB = -L$(NXRENDER_DIR)/lib -lnxrender

# Source files
SRCS = main.c file_view.c sidebar.c toolbar.c context_menu.c \
       tab_bar.c quick_look.c get_info.c \
       vfs_ops.c status_bar.c drag_drop.c search.c
OBJS = $(SRCS:.c=.o)

# Output directories
BIN_DIR = bin
LIB_DIR = lib

# Targets
BIN_TARGET = $(BIN_DIR)/path
LIB_TARGET = $(LIB_DIR)/libpath.so

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
	mkdir -p /Applications/Path.app/bin
	mkdir -p /Applications/Path.app/lib
	mkdir -p /Applications/Path.app/resources
	cp $(BIN_TARGET) /Applications/Path.app/bin/
	cp -r resources/* /Applications/Path.app/resources/
	cp manifest.npa /Applications/Path.app/
	@echo "Installed Path.app"

# Dependencies
main.o: main.c nxrender.h
file_view.o: file_view.c nxrender.h
sidebar.o: sidebar.c nxrender.h
toolbar.o: toolbar.c nxrender.h
context_menu.o: context_menu.c nxrender.h
tab_bar.o: tab_bar.c nxrender.h
quick_look.o: quick_look.c nxrender.h
get_info.o: get_info.c nxrender.h
vfs_ops.o: vfs_ops.c
status_bar.o: status_bar.c nxrender.h
drag_drop.o: drag_drop.c nxrender.h
search.o: search.c

