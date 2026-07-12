# Standalone, Qt-free spec artifact generator (see main.cpp).
# Build:
#   cmake -S tools/spec_artifacts -B tools/spec_artifacts/build -G Ninja
#   cmake --build tools/spec_artifacts/build
#   tools/spec_artifacts/build/spec_artifacts <repo-root>
cmake_minimum_required(VERSION 3.16)
project(SpecArtifacts C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DSP ${CMAKE_CURRENT_SOURCE_DIR}/../../src/dsp)
set(KFFT ${CMAKE_CURRENT_SOURCE_DIR}/../../src/third_party/kissfft)

add_executable(spec_artifacts
    main.cpp
    ${DSP}/FEC.cpp
    ${DSP}/Frame.cpp
    ${DSP}/Modulator.cpp
    ${DSP}/Preamble.cpp
    ${DSP}/Interleaver.cpp
    ${DSP}/Demodulator.cpp
    ${DSP}/DspLog.cpp
    ${KFFT}/kiss_fft.c
    ${KFFT}/kiss_fftr.c
)
target_include_directories(spec_artifacts PRIVATE ${DSP} ${KFFT})
