# NXRT - NeolyxOS Application Runtime
# CMake Build Configuration

cmake_minimum_required(VERSION 3.16)
project(nxrt VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Options
option(NXRT_BUILD_SHARED "Build shared library" ON)
option(NXRT_BUILD_STATIC "Build static library" ON)
option(NXRT_BUILD_EXAMPLES "Build examples" OFF)
option(NXRT_BUILD_TESTS "Build tests" OFF)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NXRT_LINUX TRUE)
    add_definitions(-DNXRT_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(NXRT_MACOS TRUE)
    add_definitions(-DNXRT_MACOS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(NXRT_WINDOWS TRUE)
    add_definitions(-DNXRT_WINDOWS)
endif()

# Source files
set(NXRT_SOURCES
    src/core/runtime.c
    src/ipc/ipc.c
    src/security/security.c
)

# Headers
set(NXRT_HEADERS
    include/nxrt/nxrt.h
    include/nxrt/nxrt_cpp.hpp
    include/nxrt/security.h
)

# Include directories
include_directories(include)

# Static library
if(NXRT_BUILD_STATIC)
    add_library(nxrt-static STATIC ${NXRT_SOURCES})
    target_include_directories(nxrt-static PUBLIC include)
    set_target_properties(nxrt-static PROPERTIES
        OUTPUT_NAME nxrt
        POSITION_INDEPENDENT_CODE ON
    )
    
    if(NXRT_LINUX)
        target_link_libraries(nxrt-static PRIVATE pthread)
    endif()
endif()

# Shared library
if(NXRT_BUILD_SHARED)
    add_library(nxrt-shared SHARED ${NXRT_SOURCES})
    target_include_directories(nxrt-shared PUBLIC include)
    set_target_properties(nxrt-shared PROPERTIES
        OUTPUT_NAME nxrt
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
    )
    
    if(NXRT_LINUX)
        target_link_libraries(nxrt-shared PRIVATE pthread)
    endif()
endif()

# Alias
if(NXRT_BUILD_STATIC)
    add_library(nxrt::nxrt ALIAS nxrt-static)
elseif(NXRT_BUILD_SHARED)
    add_library(nxrt::nxrt ALIAS nxrt-shared)
endif()

# Install
install(TARGETS nxrt-static nxrt-shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${NXRT_HEADERS}
    DESTINATION include/nxrt
)

# pkg-config
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/nxrt.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/nxrt.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nxrt.pc
    DESTINATION lib/pkgconfig
)
