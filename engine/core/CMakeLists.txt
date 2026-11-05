# spdlog (vcpkg, built on std::format) is private: only Log.cpp includes it.
# See docs/manual/systems/core-foundation.md.
find_package(spdlog CONFIG REQUIRED)

dg_add_module(core
    SOURCES
        src/Assert.cpp
        src/LinearAllocator.cpp
        src/Log.cpp
        src/Memory.cpp
        src/PoolAllocator.cpp
    PRIVATE_DEPS
        spdlog::spdlog)

if(ANDROID)
    target_link_libraries(dg_core PRIVATE log) # the logcat sink uses __android_log_write
endif()

target_compile_definitions(dg_core PUBLIC
    DG_VERSION_STRING="${PROJECT_VERSION}"
    DG_BUILD_CONFIG="$<CONFIG>"
    DG_CONFIG_DEBUG=$<CONFIG:Debug>
    DG_CONFIG_DEVELOPMENT=$<CONFIG:Development>
    DG_CONFIG_RELEASE=$<CONFIG:Release>
    DG_ASSERTS_ENABLED=$<NOT:$<CONFIG:Release>>
    DG_HEADLESS=$<BOOL:${DG_HEADLESS}>)
