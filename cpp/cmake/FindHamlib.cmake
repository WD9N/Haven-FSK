#[=======================================================================[
FindHamlib
----------

Locates the Hamlib rig-control library (https://github.com/Hamlib/Hamlib),
used for direct CAT control (e.g. a Kenwood TS-590SG over USB/serial)
without requiring a separately-launched `rigctld` process.

Windows: located via the HAMLIB_DIR variable (env var or -DHAMLIB_DIR=...),
pointing at an extracted official Hamlib release archive
(e.g. hamlib-w64-4.7.2.zip from https://github.com/Hamlib/Hamlib/releases).
Verified layout of that archive (downloaded and inspected directly, not
assumed): headers under include/hamlib/, runtime DLL at
bin/libhamlib-4.dll (built with MinGW-w64 GCC — same runtime DLLs,
libgcc_s_seh-1.dll/libwinpthread-1.dll, as this project's own MinGW
toolchain), and TWO import library sets under separate toolchain-specific
subdirectories: lib/gcc/ (libhamlib-4.lib, libhamlib.dll.a — for
MinGW/GCC, what this project needs) and lib/msvc/ (libhamlib-4.def only —
for MSVC, not applicable here). Not vendored into this repository; see
THIRD_PARTY_LICENSES.md for the reasoning (mirrors how Qt6 itself is
handled via QT_DIR, not vendored source like KissFFT).

Linux / Raspberry Pi: located via pkg-config against the distribution's
libhamlib-dev package (`apt install libhamlib-dev` on Debian/Raspberry Pi
OS).

Defines the imported target Hamlib::Hamlib on success. Never fatal on
its own if not found — CMakeLists.txt decides whether that's acceptable
(HAVEN_ENABLE_HAMLIB gracefully degrades rather than breaking the build
for anyone without the Hamlib SDK installed).

Result variables:
  Hamlib_FOUND        - true if Hamlib was located
  HAMLIB_INCLUDE_DIR   - directory containing hamlib/rig.h
  HAMLIB_LIBRARY       - path to the import lib / .so to link against
  HAMLIB_DLL           - (Windows only) path to the runtime DLL to bundle
#]=======================================================================]

if(WIN32)
    # HAMLIB_DIR may come from -D on the cmake command line or from the
    # environment (set alongside QT_DIR in build.bat).
    if(NOT HAMLIB_DIR AND DEFINED ENV{HAMLIB_DIR})
        set(HAMLIB_DIR "$ENV{HAMLIB_DIR}")
    endif()

    find_path(HAMLIB_INCLUDE_DIR
        NAMES hamlib/rig.h
        HINTS "${HAMLIB_DIR}/include"
    )
    # lib/gcc/ specifically (not lib/msvc/) — MinGW/GCC import libraries.
    # Two real files exist there: libhamlib-4.lib (MSVC-style .lib format —
    # NOT a GNU-format import archive despite living in the "gcc" folder)
    # and libhamlib.dll.a (the proper GNU-ld-native import library).
    # find_library()'s NAMES-based matching previously let CMake pick
    # libhamlib-4.lib first (CMake's suffix search order tried .lib before
    # .dll.a); the exe linked "successfully" against it but ended up with
    # a corrupted PE import table — a literal "(null)" DLL Name entry,
    # confirmed via `objdump -p HavenFSK.exe` — reproducing the exact
    # real launch failure this was debugging. find_file() with the exact
    # filename avoids relying on suffix-search order entirely.
    find_file(HAMLIB_LIBRARY
        NAMES libhamlib.dll.a
        HINTS "${HAMLIB_DIR}/lib/gcc"
    )
    if(NOT HAMLIB_LIBRARY)
        # Fall back to NAMES-based matching (a future Hamlib release that
        # only ships one file, or names it differently) — the .dll.a-
        # specific search above is the known-good path for 4.7.2's actual
        # on-disk layout, not a guarantee for all versions.
        find_library(HAMLIB_LIBRARY
            NAMES hamlib-4 hamlib
            HINTS "${HAMLIB_DIR}/lib/gcc"
        )
    endif()
    find_file(HAMLIB_DLL
        NAMES libhamlib-4.dll
        HINTS "${HAMLIB_DIR}/bin"
    )
    # libhamlib-4.dll's actual runtime dependencies, confirmed via
    # `objdump -p libhamlib-4.dll` (not assumed): libusb-1.0.dll,
    # libwinpthread-1.dll, plus standard always-present Windows system
    # DLLs (KERNEL32/ADVAPI32/msvcrt/WS2_32/IPHLPAPI).
    #
    # Originally assumed Qt's own windeployqt-bundled libgcc_s_seh-1.dll/
    # libwinpthread-1.dll (same filenames) would satisfy this — wrong.
    # Testing showed a real "(null).DLL was not found" launch failure;
    # comparing files found Qt's copies (May 2023, ~53-109KB) and
    # Hamlib's own bundled copies (~325-955KB) are completely different
    # builds, not interchangeable. Use the runtime DLLs Hamlib actually
    # shipped and was tested against, not whichever same-named DLL
    # happens to already be in the output directory — the "just use
    # what's there" assumption is what broke this the first time.
    find_file(HAMLIB_LIBUSB_DLL
        NAMES libusb-1.0.dll
        HINTS "${HAMLIB_DIR}/bin"
    )
    find_file(HAMLIB_LIBWINPTHREAD_DLL
        NAMES libwinpthread-1.dll
        HINTS "${HAMLIB_DIR}/bin"
    )
    find_file(HAMLIB_LIBGCC_DLL
        NAMES libgcc_s_seh-1.dll
        HINTS "${HAMLIB_DIR}/bin"
    )
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_HAMLIB QUIET hamlib)
    endif()

    find_path(HAMLIB_INCLUDE_DIR
        NAMES hamlib/rig.h
        HINTS ${PC_HAMLIB_INCLUDE_DIRS}
    )
    find_library(HAMLIB_LIBRARY
        NAMES hamlib
        HINTS ${PC_HAMLIB_LIBRARY_DIRS}
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Hamlib
    REQUIRED_VARS HAMLIB_LIBRARY HAMLIB_INCLUDE_DIR
)

if(Hamlib_FOUND AND NOT TARGET Hamlib::Hamlib)
    add_library(Hamlib::Hamlib UNKNOWN IMPORTED)
    set_target_properties(Hamlib::Hamlib PROPERTIES
        IMPORTED_LOCATION "${HAMLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HAMLIB_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(HAMLIB_INCLUDE_DIR HAMLIB_LIBRARY HAMLIB_DLL
    HAMLIB_LIBUSB_DLL HAMLIB_LIBWINPTHREAD_DLL HAMLIB_LIBGCC_DLL)
