# NeolyxOS NxUI window test - Makefile
#
# Builds nxuitest (see nxuitest.c): a C program on the NxUI C API, linked
# exactly like a REOX app (sdk/reox.mk support objects + nxrender) but
# without the REOX runtime, so it builds without the REOX compiler.
# Install it on the boot disk as /apps/nxuitest (rebuild_image.sh does this).

include ../../../sdk/reox.mk

TARGET = nxuitest
OBJS = nxuitest.o

.PHONY: all clean nxrender
all: $(TARGET)

# Always ask nxrender's own Makefile, so bridge changes are picked up
nxrender:
	$(MAKE) -C $(NXRENDER_DIR) neolyx

$(TARGET): $(OBJS) $(REOX_SUPPORT_OBJS) nxrender
	$(LD) $(NEOLYX_USER_LDFLAGS) -o $@ $(OBJS) $(REOX_SUPPORT_OBJS) $(NXRENDER_NEOLYX_LIB)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(REOX_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGET) $(REOX_BUILD_DIR)
