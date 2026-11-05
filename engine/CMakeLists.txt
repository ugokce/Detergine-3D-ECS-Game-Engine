# Engine modules in dependency order (DESIGN.md §3). Lower layers must never link higher ones.
add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(sim)
add_subdirectory(runtime)
