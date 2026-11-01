# NeolyxOS REOX SDK - GNU make include for REOX applications
#
# Usage (from an app Makefile under desktop/apps/<Name>.app/):
#
#     include ../../../sdk/reox.mk
#     REOX_SRCS  := src/main.rx
#     APP_OBJS   := $(REOX_SRCS:.rx=.o)
#     bin/app: $(APP_OBJS) $(REOX_SUPPORT_OBJS) $(REOX_LDLIBS)
#             $(LD) $(NEOLYX_USER_LDFLAGS) -o $@ $(APP_OBJS) $(REOX_SUPPORT_OBJS) $(REOX_LDLIBS)
#     %.o: %.c
#             $(CC) $(REOX_CFLAGS) -c $< -o $@
#
# Provides:
#   %.c: %.rx / %.reox     reoxc compile --emit c --target neolyx
#   REOX_CFLAGS            freestanding flags + REOX runtime + NxUI + nxrender includes
#   REOX_LDLIBS            librx_neolyx.a + libnxrender_neolyx.a
#   REOX_SUPPORT_OBJS      crt0, memory shims, NxUI, libnxwindow, libc stubs, heap (built per app)
#   REOX_RUNTIME_LIB       path of librx_neolyx.a; REOX_RUNTIME_AVAILABLE is 0/1
#
# Contract (docs/NEOLYX_ROADMAP.md, decision 3 and 6): the REOX runtime is
# split into rx_core (headers under runtime/core) and the NeolyxOS platform
# layer (headers under runtime/platform), archived as librx_neolyx.a.  Every
# REOX program needs it (strings are reference-counted rx_string values).
# When it is missing the link stops with the command that builds it;
# `make reox-runtime` runs that command.  Nothing is stubbed.

ifndef REOX_MK_INCLUDED
REOX_MK_INCLUDED := 1

# Do not let this include's rules become the app's default goal.
REOX_MK_PREV_GOAL := $(.DEFAULT_GOAL)

REOX_SDK_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
NEOLYX_ROOT  := $(abspath $(REOX_SDK_DIR)/..)

include $(REOX_SDK_DIR)/toolchain.env

# ---- tools -----------------------------------------------------------------
CC ?= gcc
LD ?= ld
AR ?= ar

# Locates reoxc (toolchain.env), checks its version and calls it with this
# command line; falls back to the legacy reoxc 1.1 spelling for old builds.
REOXC        ?= $(REOX_SDK_DIR)/tools/reoxc-neolyx
REOXC_FLAGS  ?=
REOX_TARGET  ?= neolyx

# ---- REOX runtime ----------------------------------------------------------
REOX_RUNTIME_DIR ?= $(REOX_ROOT)/reox-lang/runtime
REOX_RUNTIME_LIB ?= $(REOX_RUNTIME_DIR)/librx_neolyx.a
# core/ and platform/ are the roadmap layout; the bare runtime dir is where
# reox_runtime.h lives today (flat layout).  Harmless when a dir is absent.
REOX_RUNTIME_INCLUDES := -I$(REOX_RUNTIME_DIR)/core \
                         -I$(REOX_RUNTIME_DIR)/platform \
                         -I$(REOX_RUNTIME_DIR)

ifeq ($(wildcard $(REOX_RUNTIME_LIB)),)
REOX_RUNTIME_AVAILABLE := 0
else
REOX_RUNTIME_AVAILABLE := 1
endif
REOX_RUNTIME_LDLIB := $(REOX_RUNTIME_LIB)

# ---- NeolyxOS userspace flags (mirror desktop/Makefile) --------------------
NEOLYX_USER_CFLAGS := -Wall -Wextra -std=c11 -O2 \
                      -ffreestanding -fno-stack-protector -mno-red-zone \
                      -U_FORTIFY_SOURCE -D__NEOLYXOS__
NEOLYX_USER_LD     := $(NEOLYX_ROOT)/desktop/user.ld
NEOLYX_USER_LDFLAGS := -nostdlib -T $(NEOLYX_USER_LD)

NXRENDER_DIR        := $(NEOLYX_ROOT)/gui/nxrender_c
NXRENDER_NEOLYX_LIB := $(NXRENDER_DIR)/libnxrender_neolyx.a

# NeolyxOS headers come first: the REOX runtime ships its own reox_ffi.h
# (host SDL bridge) that must not shadow gui/nxrender_c/include/reox_ffi.h.
# kernel/include/core: nx_ipc.h, nx_fb.h and nx_abi.h, which nxsyscall.h and
# libnxwindow include.
REOX_CFLAGS := $(NEOLYX_USER_CFLAGS) \
               -I$(NXRENDER_DIR)/include \
               -I$(REOX_SDK_DIR)/nxui \
               -I$(REOX_SDK_DIR)/nxwindow \
               -I$(REOX_SDK_DIR)/crt \
               -I$(NEOLYX_ROOT)/desktop/include \
               -I$(NEOLYX_ROOT)/kernel/include/core \
               $(REOX_RUNTIME_INCLUDES)

REOX_LDLIBS := $(REOX_RUNTIME_LDLIB) $(NXRENDER_NEOLYX_LIB)

# ---- SDK support objects (built into the app's build dir) ------------------
REOX_BUILD_DIR ?= build
REOX_SDK_SUPPORT_SRCS := crt/rx_crt0_neolyx.c crt/rx_mem_neolyx.c nxui/nxui.c nxwindow/nxwindow.c
REOX_SYS_SUPPORT_SRCS := libc_stubs.c nx_malloc.c

REOX_SUPPORT_OBJS := $(addprefix $(REOX_BUILD_DIR)/sdk/,$(REOX_SDK_SUPPORT_SRCS:.c=.o)) \
                     $(addprefix $(REOX_BUILD_DIR)/sys/,$(REOX_SYS_SUPPORT_SRCS:.c=.o))

$(REOX_BUILD_DIR)/sdk/%.o: $(REOX_SDK_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(REOX_CFLAGS) -c $< -o $@

# Rebuild the support objects when the SDK headers they compile against change
REOX_SUPPORT_HEADERS := $(REOX_SDK_DIR)/nxui/nxui.h $(REOX_SDK_DIR)/nxwindow/nxwindow.h \
                        $(REOX_SDK_DIR)/nxwindow/nxwin_proto.h $(NXRENDER_DIR)/include/reox_ffi.h \
                        $(NEOLYX_ROOT)/desktop/include/nxsyscall.h
$(REOX_SUPPORT_OBJS): $(REOX_SUPPORT_HEADERS)

# Memory shims must not be "optimised" back into calls to themselves.
$(REOX_BUILD_DIR)/sdk/crt/rx_mem_neolyx.o: REOX_CFLAGS += -fno-builtin -fno-tree-loop-distribute-patterns
# libnxwindow is self-contained (sdk/nxwindow/Makefile checks it); same flags here
$(REOX_BUILD_DIR)/sdk/nxwindow/nxwindow.o: REOX_CFLAGS += -fno-builtin -fno-tree-loop-distribute-patterns

$(REOX_BUILD_DIR)/sys/%.o: $(NEOLYX_ROOT)/desktop/lib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(REOX_CFLAGS) -fno-builtin -fno-tree-loop-distribute-patterns -c $< -o $@

$(NXRENDER_NEOLYX_LIB):
	$(MAKE) -C $(NXRENDER_DIR) neolyx

# The REOX checkout is consumed read-only: a missing runtime is reported, not
# built behind the developer's back.  `make reox-runtime` builds it on request.
$(REOX_RUNTIME_LIB):
	@echo "reox.mk: missing $@" >&2
	@echo "reox.mk: build it with 'make reox-runtime' (runs: $(MAKE) -C $(REOX_RUNTIME_DIR) neolyx NEOLYX_ROOT=$(NEOLYX_ROOT))" >&2
	@exit 1

.PHONY: reox-runtime
reox-runtime:
	$(MAKE) -C $(REOX_RUNTIME_DIR) neolyx NEOLYX_ROOT=$(NEOLYX_ROOT)

# ---- REOX -> C -------------------------------------------------------------
%.c: %.rx
	$(REOXC) compile --emit c --target $(REOX_TARGET) $(REOXC_FLAGS) -o $@ $<

%.c: %.reox
	$(REOXC) compile --emit c --target $(REOX_TARGET) $(REOXC_FLAGS) -o $@ $<

.PHONY: reox-info
reox-info:
	@echo "REOXC                  = $(REOXC)"
	@echo "REOX_RUNTIME_LIB       = $(REOX_RUNTIME_LIB)"
	@echo "REOX_RUNTIME_AVAILABLE = $(REOX_RUNTIME_AVAILABLE)"
	@echo "NXRENDER_NEOLYX_LIB    = $(NXRENDER_NEOLYX_LIB)"

.DEFAULT_GOAL := $(REOX_MK_PREV_GOAL)

endif # REOX_MK_INCLUDED
