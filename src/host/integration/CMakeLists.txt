cmake_minimum_required(VERSION 3.20)
project(chimera-integration LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(chimera-integration STATIC
    ClipboardBridge.cpp
    ClipboardBridge.h
    LocationSimulator.cpp
    LocationSimulator.h
)

target_include_directories(chimera-integration PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

if(WIN32)
    target_link_libraries(chimera-integration PRIVATE
        user32
        shell32
        ole32
    )
endif()
