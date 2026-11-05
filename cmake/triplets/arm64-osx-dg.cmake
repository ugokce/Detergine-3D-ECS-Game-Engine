# macOS ARM64 packages built for the engine's deployment target (CMakePresets.json: CMAKE_OSX_DEPLOYMENT_TARGET).
# The stock arm64-osx triplet targets the host OS, which makes ld warn and can pull in newer-OS-only symbols.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
