cmake_minimum_required(VERSION 3.20)

project(HavenFSK
    VERSION 0.2.0
    DESCRIPTION "HAVEN-FSK — 16-tone MFSK HF Digital Mode"
    LANGUAGES C CXX
)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt configuration
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    WebSockets
    SerialPort
    Multimedia
    MultimediaWidgets
    Charts
)

# Add cmake helpers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Platform-specific settings
if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "resources/haven_fsk.rc")
    add_compile_definitions(HAVEN_PLATFORM_WINDOWS)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(HAVEN_PLATFORM_LINUX)
elseif(APPLE)
    add_compile_definitions(HAVEN_PLATFORM_MACOS)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/dsp/Modulator.cpp
    src/dsp/Demodulator.cpp
    src/dsp/Preamble.cpp
    src/dsp/DCD.cpp
    src/dsp/FEC.cpp
    src/dsp/Frame.cpp
    src/dsp/DspPipeline.cpp
    src/audio/AudioEngine.cpp
    src/radio/RigctldClient.cpp
    src/radio/TCIClient.cpp
    src/radio/HamlibClient.cpp
    src/radio/PTTManager.cpp
    src/ui/MainWindow.cpp
    src/ui/SettingsDialog.cpp
    src/log/LogManager.cpp
)

set(HEADERS
    src/dsp/Constants.h
    src/dsp/Modulator.h
    src/dsp/Demodulator.h
    src/dsp/Preamble.h
    src/dsp/DCD.h
    src/dsp/FEC.h
    src/dsp/Frame.h
    src/dsp/DspPipeline.h
    src/audio/AudioEngine.h
    src/audio/AudioSettings.h
    src/radio/RadioInterface.h
    src/radio/RigctldClient.h
    src/radio/TCIClient.h
    src/radio/HamlibClient.h
    src/radio/PTTManager.h
    src/radio/RadioSettings.h
    src/ui/MainWindow.h
    src/ui/SettingsDialog.h
    src/ui/StationInfoWidget.h
    src/log/LogEntry.h
    src/log/LogManager.h
)

# KissFFT — vendored, BSD-3-Clause license
add_subdirectory(src/third_party/kissfft)

# Resources
set(RESOURCES
    resources/haven_fsk.qrc
)

# Main executable
qt_add_executable(HavenFSK
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Qt libraries
target_link_libraries(HavenFSK PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::SerialPort
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Charts
    kissfft
)

# Include directories
target_include_directories(HavenFSK PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Windows: hide console window for release builds
if(WIN32)
    set_target_properties(HavenFSK PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Install rules
install(TARGETS HavenFSK
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Deploy Qt on Windows
if(WIN32)
    add_custom_command(TARGET HavenFSK POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
                "$<TARGET_FILE:HavenFSK>"
        COMMENT "Running windeployqt"
        VERBATIM
    )
endif()
