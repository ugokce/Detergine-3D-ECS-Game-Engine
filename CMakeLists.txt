cmake_minimum_required(VERSION 3.28)

# Multi-config generators (Ninja Multi-Config, Xcode, Visual Studio) must see the engine's configurations before
# project() runs. Single-config generators get CMAKE_BUILD_TYPE in DgConfigs.cmake.
if(CMAKE_GENERATOR MATCHES "Multi-Config|Xcode|Visual Studio" AND NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Development;Release" CACHE STRING "Engine build configurations")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DgVcpkg) # must run before project() so the vcpkg toolchain is picked up

project(Detergine
    VERSION 0.1.0
    DESCRIPTION "Natively deterministic, modular 3D/2D game engine"
    LANGUAGES C CXX)

include(DgOptions)
include(DgConfigs)
include(DgTargets)

# Multi-config generators append /<Config> to these, giving build/<preset>/bin/<Config>/<app>.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(engine)
add_subdirectory(apps)

if(DG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

dg_print_summary()
