@echo off
echo Building HAVEN-FSK C++...

set QT_DIR=C:\Qt\6.11.1\mingw_64
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set NINJA=C:\Qt\Tools\Ninja\ninja.exe
set MINGW=C:\Qt\Tools\mingw1310_64\bin

REM Optional: direct Hamlib rig control (CAT over USB/serial without a
REM separate rigctld process). Point this at an extracted Hamlib release
REM SDK, e.g. hamlib-w64-4.7.2.zip from
REM https://github.com/Hamlib/Hamlib/releases - if not set or not found,
REM the build proceeds without direct Hamlib support (RigctldClient and
REM TCIClient are unaffected either way).
set HAMLIB_DIR=C:\HamRadio\hamlib-w64-4.7.2

set PATH=%MINGW%;%QT_DIR%\bin;%PATH%

if not exist build mkdir build
cd build

%CMAKE% .. ^
    -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_PREFIX_PATH=%QT_DIR% ^
    -DCMAKE_MAKE_PROGRAM=%NINJA% ^
    -DHAMLIB_DIR=%HAMLIB_DIR%

if %errorlevel% neq 0 (
    echo CMake configuration failed
    exit /b 1
)

%CMAKE% --build . --parallel

if %errorlevel% neq 0 (
    echo Build failed
    exit /b 1
)

echo Build successful
echo Executable: build\HavenFSK.exe
