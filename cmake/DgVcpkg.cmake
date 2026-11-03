# vcpkg integration (included before project()).
#
# Looks for vcpkg in $VCPKG_ROOT, then in external/vcpkg (created by `dg setup --vcpkg`). If a platform toolchain is
# already set (e.g. the Android NDK), it is chain-loaded by vcpkg. Without vcpkg the engine still configures, but only
# while vcpkg.json has no dependencies.

set(_dgVcpkgRoot "")
if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(_dgVcpkgRoot "$ENV{VCPKG_ROOT}")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../external/vcpkg/scripts/buildsystems/vcpkg.cmake")
    cmake_path(ABSOLUTE_PATH CMAKE_CURRENT_LIST_DIR NORMALIZE OUTPUT_VARIABLE _dgCmakeDir)
    cmake_path(GET _dgCmakeDir PARENT_PATH _dgRepoRoot)
    set(_dgVcpkgRoot "${_dgRepoRoot}/external/vcpkg")
endif()

if(_dgVcpkgRoot)
    set(_dgVcpkgToolchain "${_dgVcpkgRoot}/scripts/buildsystems/vcpkg.cmake")
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL _dgVcpkgToolchain)
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH "Toolchain chain-loaded by vcpkg")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${_dgVcpkgToolchain}" CACHE FILEPATH "vcpkg toolchain" FORCE)
    set(DG_USING_VCPKG ON CACHE INTERNAL "")
    message(STATUS "Detergine: using vcpkg at ${_dgVcpkgRoot}")
else()
    set(DG_USING_VCPKG OFF CACHE INTERNAL "")
    message(STATUS "Detergine: vcpkg not found (set VCPKG_ROOT or run `dg setup --vcpkg`); "
                   "third-party packages are unavailable")
endif()
