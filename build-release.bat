@echo off
echo Building HAVEN-FSK C++ (RELEASE)...

set QT_DIR=C:\Qt\6.11.1\mingw_64
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set NINJA=C:\Qt\Tools\Ninja\ninja.exe
set MINGW=C:\Qt\Tools\mingw1310_64\bin

REM See build.bat for the Hamlib SDK notes - same optional dependency here.
set HAMLIB_DIR=C:\HamRadio\hamlib-w64-4.7.2

set PATH=%MINGW%;%QT_DIR%\bin;%PATH%

REM Separate build tree from the Debug one (build\) so switching between
REM dev and release builds never forces a full reconfigure/rebuild.
if not exist build-release mkdir build-release
cd build-release

%CMAKE% .. ^
    -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
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

echo Release build successful
echo Executable: build-release\HavenFSK.exe
echo Next: scripts\package-release.ps1 to stage and zip for distribution
