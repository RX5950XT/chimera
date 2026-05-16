cmake_minimum_required(VERSION 3.20)
project(chimera-graphics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)

add_library(chimera-graphics STATIC
    GraphicsBridge.cpp
    GraphicsBridge.h
    Framebuffer.cpp
    Framebuffer.h
    Renderer.cpp
    Renderer.h
    AngleBackend.cpp
    AngleBackend.h
    EglLoader.h
    FramebufferCapture.h
    AdbFramebufferCapture.cpp
    AdbFramebufferCapture.h
    GrpcFramebufferCapture.cpp
    GrpcFramebufferCapture.h
    VncFramebufferCapture.cpp
    VncFramebufferCapture.h
    HvSocketFramebufferCapture.cpp
    HvSocketFramebufferCapture.h
    PerformanceMonitor.cpp
    PerformanceMonitor.h
)

target_include_directories(chimera-graphics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Conditionally include ANGLE headers if downloaded
set(ANGLE_DIR ${CMAKE_SOURCE_DIR}/third_party/angle)
if(EXISTS ${ANGLE_DIR}/EGL/egl.h)
    target_include_directories(chimera-graphics PRIVATE ${ANGLE_DIR})
    target_compile_definitions(chimera-graphics PRIVATE CHIMERA_HAS_ANGLE=1)
    message(STATUS "ANGLE headers found at ${ANGLE_DIR}")
else()
    message(STATUS "ANGLE headers not found, using stub backend")
endif()

target_link_libraries(chimera-graphics PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::OpenGL
    OpenGL::GL
)

if(WIN32)
    target_link_libraries(chimera-graphics PRIVATE ws2_32 ole32)
endif()

# Copy ANGLE DLLs to output directory if available
set(ANGLE_BIN_DIR ${CMAKE_SOURCE_DIR}/third_party/angle/bin)
if(EXISTS ${ANGLE_BIN_DIR}/libEGL.dll AND EXISTS ${ANGLE_BIN_DIR}/libGLESv2.dll)
    add_custom_command(TARGET chimera-graphics POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ANGLE_BIN_DIR}/libEGL.dll
            ${ANGLE_BIN_DIR}/libGLESv2.dll
            $<TARGET_FILE_DIR:chimera-ui>
        COMMENT "Copying ANGLE DLLs to output directory"
    )
    message(STATUS "ANGLE libraries found, will copy to output dir")
endif()
