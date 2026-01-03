# NXAudio Server - NeolyxOS Audio Daemon
# Copyright (c) 2025 KetiveeAI

cmake_minimum_required(VERSION 3.16)
project(nxaudio-server VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    nxaudio_server.c
)

# Create server executable
add_executable(nxaudio-server ${SOURCES})

# Compile definitions
target_compile_definitions(nxaudio-server PRIVATE NXAUDIO_SERVER_MAIN)

# Math library
target_link_libraries(nxaudio-server m)

# Include paths
target_include_directories(nxaudio-server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../nxaudio/include
)

# Install
install(TARGETS nxaudio-server DESTINATION bin)
install(FILES nxaudio_server.h DESTINATION include/nxaudio)

# Systemd service file (for future)
configure_file(nxaudiod.service.in nxaudiod.service @ONLY)
