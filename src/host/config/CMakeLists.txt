cmake_minimum_required(VERSION 3.20)
project(chimera-config LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(chimera-config STATIC
    ConfigManager.cpp
    ConfigManager.h
)

target_include_directories(chimera-config PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(chimera-config PRIVATE
    nlohmann_json
)

# We'll use nlohmann/json for config parsing (header-only)
# Fallback to Qt QSettings if available
