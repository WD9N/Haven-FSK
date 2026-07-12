cmake_minimum_required(VERSION 3.20)

project(HavenFSK
    VERSION 0.6.0
    DESCRIPTION "HAVEN-FSK — 16-tone MFSK HF Digital Mode"
    LANGUAGES C CXX
)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# main.cpp gates the startup self-tests on QT_DEBUG, which qmake defines
# in debug builds but CMake does not — without this, Debug builds
# silently compiled the self-tests out and they never ran.
add_compile_definitions($<$<CONFIG:Debug>:QT_DEBUG>)

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
    Sql
)

# Add cmake helpers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Direct Hamlib linking (CAT control without a separate rigctld process —
# see DECISIONS.md for the ADR). Optional: gracefully degrades to
# HamlibClient's stub behavior if not found, never breaks the build for
# RigctldClient/TCIClient-only users. Windows: set HAMLIB_DIR to an
# extracted Hamlib release SDK. Linux/Pi: install libhamlib-dev.
option(HAVEN_ENABLE_HAMLIB "Build with direct Hamlib rig-control support" ON)
if(HAVEN_ENABLE_HAMLIB)
    find_package(Hamlib)
    if(NOT Hamlib_FOUND)
        message(WARNING
            "Hamlib not found — building without direct Hamlib rig control "
            "(RigctldClient/TCIClient are unaffected). On Windows, set "
            "HAMLIB_DIR to an extracted Hamlib release SDK "
            "(https://github.com/Hamlib/Hamlib/releases). On Linux/Raspberry "
            "Pi, install libhamlib-dev.")
        set(HAVEN_ENABLE_HAMLIB OFF)
    endif()
endif()

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
    src/dsp/SlidingDft.cpp
    src/dsp/PreambleSync.cpp
    src/dsp/DCD.cpp
    src/dsp/FEC.cpp
    src/dsp/Frame.cpp
    src/dsp/MfskModem.cpp
    src/dsp/Interleaver.cpp
    src/dsp/psk/Varicode.cpp
    src/dsp/psk/Psk31Modulator.cpp
    src/dsp/psk/Psk31Demodulator.cpp
    src/dsp/psk/Psk31Modem.cpp
    src/dsp/ModemFactory.cpp
    src/dsp/DspLog.cpp
    src/pipeline/DspPipeline.cpp
    src/audio/AudioEngine.cpp
    src/radio/RigctldClient.cpp
    src/radio/TCIClient.cpp
    src/radio/HamlibClient.cpp
    src/radio/PTTManager.cpp
    src/ui/MainWindow.cpp
    src/ui/SettingsDialog.cpp
    src/ui/RadioConfigDialog.cpp
    src/ui/RxDisplay.cpp
    src/ui/LogPanel.cpp
    src/ui/MacroPanel.cpp
    src/ui/WaterfallWidget.cpp
    src/log/LogManager.cpp
    src/log/AdifExporter.cpp
    src/ui/ExportDialog.cpp
)

set(HEADERS
    src/dsp/Constants.h
    src/dsp/MfskConstants.h
    src/dsp/IModem.h
    src/dsp/ModemFactory.h
    src/dsp/MfskModem.h
    src/dsp/Interleaver.h
    src/dsp/psk/Psk31Constants.h
    src/dsp/psk/Psk31VaricodeTable.h
    src/dsp/psk/Varicode.h
    src/dsp/psk/Psk31Modulator.h
    src/dsp/psk/Psk31Demodulator.h
    src/dsp/psk/Psk31Modem.h
    src/dsp/psk/Psk31SelfTest.h
    src/dsp/Modulator.h
    src/dsp/Demodulator.h
    src/dsp/Preamble.h
    src/dsp/SlidingDft.h
    src/dsp/PreambleSync.h
    src/dsp/DCD.h
    src/dsp/FEC.h
    src/dsp/Frame.h
    src/dsp/FieldMarkers.h
    src/dsp/DspLog.h
    src/pipeline/DspPipeline.h
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
    src/ui/RadioConfigDialog.h
    src/ui/StationInfoWidget.h
    src/ui/RxDisplay.h
    src/ui/LogPanel.h
    src/ui/MacroPanel.h
    src/ui/WaterfallWidget.h
    src/ui/FrequencyControl.h
    src/ui/LevelPanel.h
    src/log/LogEntry.h
    src/log/LogManager.h
    src/log/AdifExporter.h
    src/ui/ExportDialog.h
)

# KissFFT — vendored, BSD-3-Clause license
add_subdirectory(src/third_party/kissfft)

# Resources
set(RESOURCES
    resources/haven_fsk.qrc
)

# Main executable. APP_ICON_RESOURCE_WINDOWS (set in the platform block
# above) compiles resources/haven_fsk.rc into the exe so Explorer/taskbar
# show the HAVEN-FSK icon; it is empty/undefined on non-Windows.
qt_add_executable(HavenFSK
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
    ${APP_ICON_RESOURCE_WINDOWS}
)

# Qt libraries
target_link_libraries(HavenFSK PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::SerialPort
    Qt6::Multimedia
    Qt6::Sql
    kissfft
)

# Direct Hamlib rig control (optional — see HAVEN_ENABLE_HAMLIB above)
if(HAVEN_ENABLE_HAMLIB)
    target_link_libraries(HavenFSK PRIVATE Hamlib::Hamlib)
    target_compile_definitions(HavenFSK PRIVATE HAVEN_HAMLIB_ENABLED)
    if(WIN32)
        # Copy libhamlib-4.dll and its full runtime dependency set as
        # Hamlib itself shipped them (see cmake/FindHamlib.cmake for why
        # this must be Hamlib's own libwinpthread-1.dll/libgcc_s_seh-1.dll,
        # not whichever same-named DLL windeployqt already placed here —
        # confirmed by testing that assumption caused a real launch
        # failure). copy_if_different means Hamlib's copies take
        # precedence over Qt's for these two filenames.
        foreach(dll IN ITEMS
                HAMLIB_DLL HAMLIB_LIBUSB_DLL
                HAMLIB_LIBWINPTHREAD_DLL HAMLIB_LIBGCC_DLL)
            if(${dll})
                add_custom_command(TARGET HavenFSK POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "${${dll}}" "$<TARGET_FILE_DIR:HavenFSK>"
                    COMMENT "Copying ${dll} (Hamlib runtime dependency)"
                )
            endif()
        endforeach()
    endif()
endif()

# Include directories
target_include_directories(HavenFSK PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Windowed app (no console) for release packaging; Debug keeps the
# console so startup self-test output stays visible during development.
if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(HavenFSK PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Install rules
install(TARGETS HavenFSK
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Deploy Qt on Windows. MSYS2 names the tool windeployqt6; skip with a
# notice when neither exists (CI builds don't need a deployable tree).
if(WIN32)
    find_program(HAVEN_WINDEPLOYQT
        NAMES windeployqt windeployqt6
        HINTS "${Qt6_DIR}/../../../bin")
    if(HAVEN_WINDEPLOYQT)
        add_custom_command(TARGET HavenFSK POST_BUILD
            COMMAND "${HAVEN_WINDEPLOYQT}" "$<TARGET_FILE:HavenFSK>"
            COMMENT "Running windeployqt"
            VERBATIM
        )
    else()
        message(STATUS "windeployqt not found — skipping Qt deploy step")
    endif()
endif()
