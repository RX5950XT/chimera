cmake_minimum_required(VERSION 3.20)
project(chimera-integration-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)

enable_testing()

function(add_integration_test name src)
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
    # Integration tests are excluded from the default ctest run;
    # run explicitly with: ctest -L integration
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "integration" TIMEOUT 120)
endfunction()

add_integration_test(itest-emulator-boot   test_emulator_boot.cpp)
add_integration_test(itest-input-inject    test_input_inject.cpp)
add_integration_test(itest-screencap       test_screencap.cpp     chimera-graphics)
