# KissFFT — BSD-3-Clause license
# Copyright (c) 2003-2010 Mark Borgerding
# License text: see THIRD_PARTY_LICENSES.md in the repository root
# DO NOT remove or modify copyright headers from kiss_fft.h, kiss_fft.c,
# kiss_fftr.h, or kiss_fftr.c — required for BSD-3-Clause compliance.
add_library(kissfft STATIC kiss_fft.c kiss_fftr.c)
target_include_directories(kissfft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(kissfft PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
