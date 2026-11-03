# Deterministic simulation layer: strict floating-point flags via SIM (CLAUDE.md determinism rules).
dg_add_module(sim SIM
    SOURCES
        src/Simulation.cpp
    PUBLIC_DEPS
        dg::core)
