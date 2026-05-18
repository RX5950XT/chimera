cmake_minimum_required(VERSION 3.20)
project(chimera-storage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(chimera-storage STATIC
    SharedFolder.cpp
    SharedFolder.h
)

target_include_directories(chimera-storage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(chimera-storage PRIVATE
    Qt6::Core
    chimera-instance
)
