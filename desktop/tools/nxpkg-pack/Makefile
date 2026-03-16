# nxpkg-pack - NeolyxOS Packager Tool

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lz

TARGET = nxpkg-pack
SRCS = src/main.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/
	@echo "Installed nxpkg-pack to /usr/local/bin/"

test: $(TARGET)
	@echo "=== Testing nxpkg-pack ==="
	mkdir -p /tmp/TestApp.app/bin /tmp/TestApp.app/resources
	echo '{"name":"TestApp","bundle_id":"com.test.app","version":"1.0"}' > /tmp/TestApp.app/manifest.npa
	echo "#!/bin/sh" > /tmp/TestApp.app/bin/testapp
	./$(TARGET) /tmp/TestApp.app -o /tmp/
	@test -f /tmp/TestApp.nxpkg && echo "SUCCESS: Created /tmp/TestApp.nxpkg" || echo "FAILED"
	hexdump -C /tmp/TestApp.nxpkg | head -2
	rm -rf /tmp/TestApp.app /tmp/TestApp.nxpkg
