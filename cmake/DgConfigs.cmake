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

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(CheckIPOSupported)
check_ipo_supported(RESULT DG_IPO_SUPPORTED LANGUAGES CXX)
if(DG_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
