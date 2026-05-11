cmake_minimum_required(VERSION 3.20)
project(chimera-instance LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(chimera-instance STATIC
    InstanceManager.cpp
    InstanceManager.h
    VirtualMachine.cpp
    VirtualMachine.h
    ProcessLauncher.cpp
    ProcessLauncher.h
    DeviceSpoofer.cpp
    DeviceSpoofer.h
    MemoryTrimmer.cpp
    MemoryTrimmer.h
    DiskCompactor.cpp
    DiskCompactor.h
    HyperVManager.cpp
    HyperVManager.h
)

target_include_directories(chimera-instance PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(chimera-instance PRIVATE
    chimera-config
    nlohmann_json
    Qt6::Core
)

if(WIN32)
    target_link_libraries(chimera-instance PRIVATE
        psapi
        ws2_32
    )
endif()
