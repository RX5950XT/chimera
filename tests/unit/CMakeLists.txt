cmake_minimum_required(VERSION 3.20)
project(chimera-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)

enable_testing()

# Helper binary: prints argv[1..] to stdout (used by test-process-launcher)
add_executable(echo_args ${CMAKE_CURRENT_SOURCE_DIR}/../helpers/echo_args.cpp)
set_target_properties(echo_args PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/Debug
)

if(WIN32)
    add_executable(shared_d3d11_texture_producer
        ${CMAKE_CURRENT_SOURCE_DIR}/../helpers/shared_d3d11_texture_producer.cpp
    )
    target_include_directories(shared_d3d11_texture_producer PRIVATE
        ${CMAKE_SOURCE_DIR}/src/host/graphics
    )
    target_link_libraries(shared_d3d11_texture_producer PRIVATE
        Qt6::Core
        d3d11
        dxgi
    )
    set_target_properties(shared_d3d11_texture_producer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/Debug
    )
endif()

function(add_chimera_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Test
        chimera-config
        chimera-input
        chimera-instance
        chimera-utils
        ${ARGN}
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_chimera_test(test-config-manager test_config_manager.cpp)
add_chimera_test(test-input-mapper test_input_mapper.cpp)
add_chimera_test(test-instance-manager test_instance_manager.cpp)
add_chimera_test(test-virtual-machine test_virtual_machine.cpp)
add_chimera_test(test-graphics-framebuffer test_graphics_framebuffer.cpp chimera-graphics)
add_chimera_test(test-adb-framebuffer-capture test_adb_framebuffer_capture.cpp chimera-graphics)
add_chimera_test(test-grpc-framebuffer-capture test_grpc_framebuffer_capture.cpp chimera-graphics)
add_chimera_test(test-shared-memory-framebuffer-capture test_shared_memory_framebuffer_capture.cpp chimera-graphics)
add_chimera_test(test-shared-d3d11-texture-capture test_shared_d3d11_texture_capture.cpp chimera-graphics)
if(WIN32)
    target_link_libraries(test-shared-d3d11-texture-capture PRIVATE d3d11 dxgi)
endif()
add_chimera_test(test-qmp-input test_qmp_input.cpp)
add_chimera_test(test-process-launcher test_process_launcher.cpp)
add_dependencies(test-process-launcher echo_args)
add_chimera_test(test-android-console-input test_android_console_input.cpp)
add_chimera_test(test-coordinate-mapper test_coordinate_mapper.cpp)
add_chimera_test(test-clipboard-bridge test_clipboard_bridge.cpp chimera-integration)
add_chimera_test(test-location-simulator test_location_simulator.cpp chimera-integration)
add_chimera_test(test-device-spoofer test_device_spoofer.cpp chimera-instance)
add_chimera_test(test-macro-engine test_macro_engine.cpp chimera-input)
add_chimera_test(test-gamepad-manager test_gamepad_manager.cpp chimera-input)
add_chimera_test(test-audio-bridge test_audio_bridge.cpp chimera-audio)
