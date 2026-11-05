# Build configurations (DESIGN.md §15):
#   Debug        no optimization, asserts, full debug info
#   Development  optimized with debug info and asserts; the default daily configuration
#   Release      fully optimized (+LTO), no asserts; used for shipping builds

set(DG_CONFIGURATIONS Debug Development Release)

get_property(_dgMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _dgMultiConfig)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Development CACHE STRING "Build configuration" FORCE)
    endif()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${DG_CONFIGURATIONS})
endif()

# Development is a custom configuration, so it needs its own flag variables. It starts from RelWithDebInfo.
foreach(_lang C CXX)
    set(CMAKE_${_lang}_FLAGS_DEVELOPMENT "${CMAKE_${_lang}_FLAGS_RELWITHDEBINFO}"
        CACHE STRING "${_lang} flags for the Development configuration")
endforeach()
foreach(_kind EXE SHARED MODULE STATIC)
    set(CMAKE_${_kind}_LINKER_FLAGS_DEVELOPMENT "${CMAKE_${_kind}_LINKER_FLAGS_RELWITHDEBINFO}"
        CACHE STRING "${_kind} linker flags for the Development configuration")
endforeach()
# Imported packages (vcpkg) only ship Debug/Release; Development links their optimized variant.
set(CMAKE_MAP_IMPORTED_CONFIG_DEVELOPMENT RelWithDebInfo Release "")

# Xcode turns on libc++ debug hardening at -O0. That changes libc++'s ABI tags, so our Debug objects and the
# vcpkg-built libraries (built without hardening, as are all Ninja builds) end up mixing incompatible inline
# std::vector code at link time (a heap overflow inside GoogleTest, found under ASan). Keep one mode everywhere.
set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_STANDARD_LIBRARY_HARDENING "none")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(CheckIPOSupported)
check_ipo_supported(RESULT DG_IPO_SUPPORTED LANGUAGES CXX)
if(DG_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The engine does not use C++20 modules. Without this, Ninja + Clang scans every source for module dependencies
# (needs clang-scan-deps, and costs an extra pass per file).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
