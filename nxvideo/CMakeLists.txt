# NXVideo - Native Video Rendering System
# CMake Build Configuration

cmake_minimum_required(VERSION 3.16)
project(nxvideo VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Options
option(NXVIDEO_BUILD_SHARED "Build shared library" ON)
option(NXVIDEO_BUILD_STATIC "Build static library" ON)
option(NXVIDEO_BUILD_EXAMPLES "Build examples" OFF)
option(NXVIDEO_BUILD_TESTS "Build tests" OFF)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NXVIDEO_LINUX TRUE)
    add_definitions(-DNXVIDEO_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(NXVIDEO_MACOS TRUE)
    add_definitions(-DNXVIDEO_MACOS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(NXVIDEO_WINDOWS TRUE)
    add_definitions(-DNXVIDEO_WINDOWS)
endif()

# Source files
set(NXVIDEO_SOURCES
    src/core/init.c
    src/core/player.c
)

# Headers
set(NXVIDEO_HEADERS
    include/nxvideo/nxvideo.h
    include/nxvideo/nxvideo_cpp.hpp
)

# Include directories
include_directories(include)

# Static library
if(NXVIDEO_BUILD_STATIC)
    add_library(nxvideo-static STATIC ${NXVIDEO_SOURCES})
    target_include_directories(nxvideo-static PUBLIC include)
    set_target_properties(nxvideo-static PROPERTIES
        OUTPUT_NAME nxvideo
        POSITION_INDEPENDENT_CODE ON
    )
    
    if(NXVIDEO_LINUX)
        target_link_libraries(nxvideo-static PRIVATE pthread dl)
    endif()
endif()

# Shared library
if(NXVIDEO_BUILD_SHARED)
    add_library(nxvideo-shared SHARED ${NXVIDEO_SOURCES})
    target_include_directories(nxvideo-shared PUBLIC include)
    set_target_properties(nxvideo-shared PROPERTIES
        OUTPUT_NAME nxvideo
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
    )
    
    if(NXVIDEO_LINUX)
        target_link_libraries(nxvideo-shared PRIVATE pthread dl)
    endif()
endif()

# Alias for convenience
if(NXVIDEO_BUILD_STATIC)
    add_library(nxvideo::nxvideo ALIAS nxvideo-static)
elseif(NXVIDEO_BUILD_SHARED)
    add_library(nxvideo::nxvideo ALIAS nxvideo-shared)
endif()

# Install
install(TARGETS nxvideo-static nxvideo-shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${NXVIDEO_HEADERS}
    DESTINATION include/nxvideo
)

# pkg-config
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/nxvideo.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/nxvideo.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nxvideo.pc
    DESTINATION lib/pkgconfig
)

# Tests
if(NXVIDEO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(NXVIDEO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
