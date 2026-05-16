cmake_minimum_required(VERSION 3.20)
project(chimera-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)

enable_testing()

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
add_chimera_test(test-graphics-framebuffer test_graphics_framebuffer.cpp chimera-graphics)
add_chimera_test(test-adb-framebuffer-capture test_adb_framebuffer_capture.cpp chimera-graphics)
add_chimera_test(test-qmp-input test_qmp_input.cpp)
