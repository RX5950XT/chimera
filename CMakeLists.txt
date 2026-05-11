cmake_minimum_required(VERSION 3.20)
project(Chimera VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(CHIMERA_BUILD_TESTS "Build tests" ON)
option(CHIMERA_BUILD_QT_UI "Build Qt6 UI" ON)

# nlohmann/json (header-only, bundled)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_SOURCE_DIR}/third_party)

# ---------------------------------------------------------------------------
# Host Layer
# ---------------------------------------------------------------------------
if(CHIMERA_BUILD_QT_UI)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2)
    qt_standard_project_setup()
endif()

# Common utilities
add_subdirectory(src/common/utils)

# Host modules
add_subdirectory(src/host/config)
add_subdirectory(src/host/input)
add_subdirectory(src/host/graphics)
add_subdirectory(src/host/audio)
add_subdirectory(src/host/storage)
add_subdirectory(src/host/instance)
add_subdirectory(src/host/integration)

if(CHIMERA_BUILD_QT_UI)
    add_subdirectory(src/host/ui)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CHIMERA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/integration/CMakeLists.txt")
        add_subdirectory(tests/integration)
    endif()
endif()

# ---------------------------------------------------------------------------
# Package / Install
# ---------------------------------------------------------------------------
install(DIRECTORY configs/ DESTINATION etc/chimera)
install(DIRECTORY third_party/ DESTINATION lib/chimera/third_party)
