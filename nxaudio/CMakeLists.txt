# NXAudio CMake Build System
# Copyright (c) 2025 KetiveeAI - KETIVEEAI License

cmake_minimum_required(VERSION 3.15)
project(nxaudio VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(NXAUDIO_BUILD_DAEMON "Build audio daemon" ON)
option(NXAUDIO_BUILD_TESTS "Build test suite" ON)
option(NXAUDIO_BUILD_EXAMPLES "Build examples" ON)
option(NXAUDIO_ENABLE_SIMD "Enable SIMD optimizations" ON)

# Detect platform
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NXAUDIO_PLATFORM_LINUX ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "NeolyxOS")
    set(NXAUDIO_PLATFORM_NEOLYX ON)
endif()

# Source files
set(NXAUDIO_CORE_SOURCES
    src/core/init.c
)

set(NXAUDIO_SPATIAL_SOURCES
    src/spatial/hrtf.c
    src/spatial/mixer.c
)

set(NXAUDIO_ALL_SOURCES
    ${NXAUDIO_CORE_SOURCES}
    ${NXAUDIO_SPATIAL_SOURCES}
)

# Main library
add_library(nxaudio SHARED ${NXAUDIO_ALL_SOURCES})
add_library(nxaudio_static STATIC ${NXAUDIO_ALL_SOURCES})

# Include directories
target_include_directories(nxaudio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(nxaudio_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link math library
target_link_libraries(nxaudio PRIVATE m)
target_link_libraries(nxaudio_static PRIVATE m)

# SIMD optimizations
if(NXAUDIO_ENABLE_SIMD)
    include(CheckCCompilerFlag)
    
    check_c_compiler_flag("-mavx2" HAVE_AVX2)
    if(HAVE_AVX2)
        target_compile_options(nxaudio PRIVATE -mavx2)
        target_compile_definitions(nxaudio PRIVATE NXAUDIO_HAVE_AVX2)
    endif()
    
    check_c_compiler_flag("-msse4.1" HAVE_SSE41)
    if(HAVE_SSE41)
        target_compile_options(nxaudio PRIVATE -msse4.1)
        target_compile_definitions(nxaudio PRIVATE NXAUDIO_HAVE_SSE41)
    endif()
endif()

# Version info
target_compile_definitions(nxaudio PRIVATE
    NXAUDIO_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    NXAUDIO_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    NXAUDIO_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# Set library version
set_target_properties(nxaudio PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Installation
include(GNUInstallDirs)

install(TARGETS nxaudio nxaudio_static
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/nxaudio
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# pkgconfig
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/nxaudio.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/nxaudio.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nxaudio.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Tests
if(NXAUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()

# Examples
if(NXAUDIO_BUILD_EXAMPLES)
    add_subdirectory(examples EXCLUDE_FROM_ALL)
endif()

# Summary
message(STATUS "")
message(STATUS "NXAudio Configuration:")
message(STATUS "  Version:       ${PROJECT_VERSION}")
message(STATUS "  Platform:      ${CMAKE_SYSTEM_NAME}")
message(STATUS "  SIMD:          ${NXAUDIO_ENABLE_SIMD}")
message(STATUS "  Build daemon:  ${NXAUDIO_BUILD_DAEMON}")
message(STATUS "  Build tests:   ${NXAUDIO_BUILD_TESTS}")
message(STATUS "")
