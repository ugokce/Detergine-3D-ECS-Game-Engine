# Application host: command line, fixed-tick main loop, shutdown handling.
dg_add_module(runtime
    SOURCES
        src/Application.cpp
    PUBLIC_DEPS
        dg::core
        dg::sim)
