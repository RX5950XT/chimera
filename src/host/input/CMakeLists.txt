cmake_minimum_required(VERSION 3.20)
project(chimera-input LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(chimera-input STATIC
    AndroidConsoleInput.cpp
    AndroidConsoleInput.h
    CoordinateMapper.h
    InputBridge.cpp
    InputBridge.h
    InputMapper.cpp
    InputMapper.h
    GamepadManager.cpp
    GamepadManager.h
    MacroEngine.cpp
    MacroEngine.h
    QmpInput.cpp
    QmpInput.h
    VirtioInput.cpp
    VirtioInput.h
    HvSocketTransport.cpp
    HvSocketTransport.h
)

target_include_directories(chimera-input PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(chimera-input PRIVATE
    chimera-config
    nlohmann_json
    Qt6::Core
    Qt6::Network
)

if(WIN32)
    target_link_libraries(chimera-input PRIVATE
        xinput
        dinput8
        dxguid
        ws2_32
        ole32
    )
endif()
