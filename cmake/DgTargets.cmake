# Target helpers. Every engine target goes through these so flags, warnings and determinism settings stay uniform.

# Common settings for every engine target.
function(dg_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_23)

    if(MSVC) # also clang-cl
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target} PRIVATE /Zc:__cplusplus /Zc:preprocessor)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual)
    endif()

    if(DG_WARNINGS_AS_ERRORS)
        set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
    endif()
endfunction()

# Strict floating-point settings for deterministic simulation code (CLAUDE.md determinism rule 3).
# These flags must be identical in every configuration so Debug and Release builds produce bit-identical sim results.
function(dg_sim_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:precise)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang") # clang-cl contracts to FMA unless told not to
            target_compile_options(${target} PRIVATE /clang:-ffp-contract=off)
        endif()
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
    set_property(TARGET ${target} PROPERTY DG_SIM_TARGET ON)
endfunction()

# dg_add_module(<name> [SIM] SOURCES ... [PUBLIC_DEPS ...] [PRIVATE_DEPS ...])
# Creates the static library dg_<name> (alias dg::<name>) with public headers in <module>/include.
function(dg_add_module name)
    cmake_parse_arguments(ARG "SIM" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
    set(target dg_${name})
    add_library(${target} STATIC ${ARG_SOURCES})
    add_library(dg::${name} ALIAS ${target})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
    dg_configure_target(${target})
    if(ARG_SIM)
        dg_sim_target(${target})
    endif()
    set_target_properties(${target} PROPERTIES FOLDER "engine")
endfunction()

# dg_add_app(<name> SOURCES ... [DEPS ...])
function(dg_add_app name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_DEPS})
    dg_configure_target(${name})
    set_target_properties(${name} PROPERTIES FOLDER "apps")
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(${name} PROPERTIES XCODE_GENERATE_SCHEME ON)
    endif()
endfunction()

function(dg_print_summary)
    get_property(_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_multi)
        set(_configs "${CMAKE_CONFIGURATION_TYPES}")
    else()
        set(_configs "${CMAKE_BUILD_TYPE}")
    endif()
    message(STATUS "Detergine ${PROJECT_VERSION}: generator='${CMAKE_GENERATOR}' configs='${_configs}' "
                   "compiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
                   "editor=${DG_BUILD_EDITOR} server=${DG_BUILD_SERVER} headless=${DG_HEADLESS} "
                   "tests=${DG_BUILD_TESTS} vcpkg=${DG_USING_VCPKG}")
endfunction()
