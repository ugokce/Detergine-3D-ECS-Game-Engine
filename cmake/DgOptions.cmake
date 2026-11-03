# Engine-wide build options. Presets set these; see CMakePresets.json.

option(DG_BUILD_EDITOR "Build the editor (desktop only)" ON)
option(DG_BUILD_TESTS "Build unit and determinism tests" ON)
option(DG_HEADLESS "Headless build profile: no renderer, audio, UI or editor (DESIGN.md §15.1)" OFF)
option(DG_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (always ON in CI)" OFF)

# Normal variable shadows the cache entry, so toggling DG_HEADLESS back off restores the user's choice.
if(ANDROID OR IOS OR DG_HEADLESS)
    set(DG_BUILD_EDITOR OFF)
endif()

if(ANDROID OR IOS)
    set(DG_BUILD_SERVER OFF)
else()
    set(DG_BUILD_SERVER ON)
endif()
