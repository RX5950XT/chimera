cmake_minimum_required(VERSION 3.20)
project(chimera-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

enable_testing()

function(add_chimera_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE
        Qt6::Core
        Qt6::Test
        chimera-config
        chimera-input
        chimera-instance
        chimera-utils
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_chimera_test(test-config-manager test_config_manager.cpp)
add_chimera_test(test-input-mapper test_input_mapper.cpp)
add_chimera_test(test-instance-manager test_instance_manager.cpp)
