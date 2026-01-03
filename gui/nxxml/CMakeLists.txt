cmake_minimum_required(VERSION 3.16)
project(nxxml VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources
set(NXXML_SOURCES
    src/parser.cpp
)

# Create library
add_library(nxxml STATIC ${NXXML_SOURCES})

target_include_directories(nxxml PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Compiler warnings
target_compile_options(nxxml PRIVATE
    -Wall -Wextra -Wpedantic
)

# Install
install(TARGETS nxxml ARCHIVE DESTINATION lib)
install(FILES include/nxxml.h DESTINATION include)
