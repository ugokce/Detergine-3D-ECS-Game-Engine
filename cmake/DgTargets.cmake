# Target helpers. Every engine target goes through these so flags, warnings and determinism settings stay uniform.

# Strict floating point for every engine target (CLAUDE.md determinism rule 3). Applied engine-wide, not only to sim
# targets: an inline math function compiled with different FP flags in two translation units is an ODR hazard, and the
# linker may keep the contracted (FMA) copy for sim code. Identical in every configuration, so Debug == Release.
function(dg_strict_fp target)
    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # clang-cl: its default FP model is already precise but still contracts to FMA. Passing /fp:precise as well
        # makes clang-cl warn that -ffp-contract=off overrides it (an error under warnings-as-errors).
        target_compile_options(${target} PRIVATE /clang:-ffp-contract=off)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
endfunction()

# Common settings for every engine target.
function(dg_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    dg_strict_fp(${target})

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

# Marks deterministic simulation code. The strict FP flags already come from dg_configure_target; the property lets
# tooling (and future checks such as banned-function lints) find sim targets.
function(dg_sim_target target)
    dg_strict_fp(${target})
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
