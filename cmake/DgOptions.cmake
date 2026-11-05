# Engine-wide build options. Presets set these; see CMakePresets.json.

option(DG_BUILD_EDITOR "Build the editor (desktop only)" ON)
option(DG_BUILD_TESTS "Build unit and determinism tests" ON)
option(DG_HEADLESS "Headless build profile: no renderer, audio, UI or editor (DESIGN.md §15.1)" OFF)
option(DG_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (always ON in CI)" OFF)
option(DG_ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(DG_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer (aborts on the first error)" OFF)

if(DG_ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()
if(DG_ENABLE_UBSAN AND NOT MSVC)
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Normal variable shadows the cache entry, so toggling DG_HEADLESS back off restores the user's choice.
if(ANDROID OR IOS OR DG_HEADLESS)
    set(DG_BUILD_EDITOR OFF)
endif()

if(ANDROID OR IOS)
    set(DG_BUILD_SERVER OFF)
else()
    set(DG_BUILD_SERVER ON)
endif()
