dg_add_module(core
    SOURCES
        src/Assert.cpp
        src/Log.cpp)

target_compile_definitions(dg_core PUBLIC
    DG_VERSION_STRING="${PROJECT_VERSION}"
    DG_BUILD_CONFIG="$<CONFIG>"
    DG_CONFIG_DEBUG=$<CONFIG:Debug>
    DG_CONFIG_DEVELOPMENT=$<CONFIG:Development>
    DG_CONFIG_RELEASE=$<CONFIG:Release>
    DG_ASSERTS_ENABLED=$<NOT:$<CONFIG:Release>>
    DG_HEADLESS=$<BOOL:${DG_HEADLESS}>)
