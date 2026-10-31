# NeolyxOS - NXCanvas host tests
#
# Builds engine modules with the HOST compiler under ASan/UBSan.
#   make run T=core SRCS="nxc_core.c nxc_math.c"
# builds test_core.c against the listed engine sources and runs it.
#   make all-tests   runs every test registered in TESTS below.
# Output images go to $(OUT) (default ./out).

HOSTCC  ?= gcc
OUT     ?= out
CFLAGS  = -std=c11 -O1 -g -Wall -Wextra -Werror=implicit-function-declaration \
          -fsanitize=address,undefined -fno-omit-frame-pointer -D_POSIX_C_SOURCE=200809L \
          -DNX_HOST_PREVIEW
LDFLAGS = -fsanitize=address,undefined -lm

ENGINE  = ..
BIN     = $(OUT)/bin

.PHONY: run all-tests clean

run:
	@test -n "$(T)" || (echo "usage: make run T=<name> SRCS=\"<engine .c files>\""; exit 2)
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $(BIN)/test_$(T) test_$(T).c nxc_test.c $(addprefix $(ENGINE)/,$(SRCS)) $(LDFLAGS)
	NXC_TEST_OUT=$(OUT) $(BIN)/test_$(T)

# name:engine-sources (comma separated). Each module owner appends its line.
TESTS = core:nxc_core.c,nxc_math.c \
        shapes:nxc_core.c,nxc_math.c,nxc_paint.c,nxc_shapes.c \
        raster:nxc_core.c,nxc_math.c,nxc_paint.c,nxc_raster.c \
        effects:nxc_core.c,nxc_math.c,nxc_paint.c,nxc_effects.c,nxc_image.c,nxc_shapes.c,nxc_raster.c \
        font:nxc_core.c,nxc_math.c,nxc_paint.c,nxc_font.c,nxc_raster.c \
        text:nxc_core.c,nxc_math.c,nxc_paint.c,nxc_raster.c,nxc_font.c,nxc_text.c,nxc_shapes.c,nxc_image.c,nxc_effects.c \
        quality:nxc_core.c,nxc_math.c,nxc_paint.c,nxc_raster.c,nxc_font.c,nxc_text.c,nxc_shapes.c,nxc_image.c,nxc_effects.c \
        registry:nxc_core.c,nxc_math.c,nxc_paint.c,nxc_raster.c,nxc_font.c,nxc_text.c,../nxfont_registry.c

all-tests:
	@set -e; for t in $(TESTS); do \
	  name=$${t%%:*}; srcs=$$(echo $${t#*:} | tr ',' ' '); \
	  $(MAKE) --no-print-directory run T=$$name SRCS="$$srcs"; \
	done

clean:
	rm -rf $(OUT)
