cmake_minimum_required(VERSION 3.20)
project(chimera-utils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(chimera-utils STATIC
    Logger.cpp
    Logger.h
    ThreadPool.cpp
    ThreadPool.h
    FileUtils.cpp
    FileUtils.h
    LowInterferenceProcess.cpp
    LowInterferenceProcess.h
)

target_include_directories(chimera-utils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(chimera-utils PRIVATE
    Qt6::Core
)
