# Math: deterministic scalar math (fixed point, detmath) and vector/quaternion/matrix types (DESIGN.md §5.2).
dg_add_module(math SIM
    SOURCES
        src/DetMath.cpp
        src/Fixed.cpp
        src/Vector.cpp
    PUBLIC_DEPS
        dg::core)
