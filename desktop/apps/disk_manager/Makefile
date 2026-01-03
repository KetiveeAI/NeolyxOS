# NeolyxOS Disk Manager
# Build with REOX compiler

REOX_COMPILER = reoxc
TARGET = disk_manager

.PHONY: all clean run

all: $(TARGET)

$(TARGET): disk_manager.rx
	$(REOX_COMPILER) disk_manager.rx -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
