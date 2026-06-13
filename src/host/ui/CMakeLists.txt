cmake_minimum_required(VERSION 3.20)
project(ChimeraHostUI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 OpenGL Network LabsPlatform LabsSettings)

qt_add_executable(chimera-ui
    main.cpp
    ChimeraWindow.cpp
    ChimeraWindow.h
    GuestDisplay.cpp
    GuestDisplay.h
    NativeEmulatorView.cpp
    NativeEmulatorView.h
    QmlAndroidControls.cpp
    QmlAndroidControls.h
    QmlInstanceManager.cpp
    QmlInstanceManager.h
    QmlMacroEngine.cpp
    QmlMacroEngine.h
    QmlInputMapper.cpp
    QmlInputMapper.h
    ScreenRecorder.cpp
    ScreenRecorder.h
    ChimeraWindow.qml
    InputMapperOverlay.cpp
    InputMapperOverlay.h
    resources.qrc
)

target_include_directories(chimera-ui PRIVATE
    ${CMAKE_SOURCE_DIR}/src/host/audio
)

target_link_libraries(chimera-ui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::OpenGL
    Qt6::Network
    Qt6::LabsPlatform
    Qt6::LabsSettings
    chimera-config
    chimera-input
    chimera-graphics
    chimera-audio
    chimera-instance
    chimera-integration
    chimera-storage
    chimera-utils
    nlohmann_json
)

# ANGLE / OpenGL integration will be linked here later

if(WIN32)
    get_filename_component(QT_ROOT_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
    set(QT_WINDEPLOYQT_EXECUTABLE "${QT_ROOT_DIR}/bin/windeployqt.exe")

    if(EXISTS "${QT_WINDEPLOYQT_EXECUTABLE}")
        add_custom_command(TARGET chimera-ui POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}"
                --no-compiler-runtime
                --no-translations
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
                "$<TARGET_FILE:chimera-ui>"
            COMMENT "Deploying Qt runtime for chimera-ui"
        )
    else()
        message(WARNING "windeployqt.exe not found; chimera-ui.exe may require Qt in PATH")
    endif()
endif()

# Copy bundled FFmpeg to output directory if available
set(FFMPEG_BIN ${CMAKE_SOURCE_DIR}/third_party/ffmpeg/ffmpeg.exe)
if(EXISTS ${FFMPEG_BIN})
    add_custom_command(TARGET chimera-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FFMPEG_BIN}
            $<TARGET_FILE_DIR:chimera-ui>
        COMMENT "Copying bundled FFmpeg to output directory"
    )
endif()

if(WIN32)
    set_target_properties(chimera-ui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS chimera-ui RUNTIME DESTINATION bin)
