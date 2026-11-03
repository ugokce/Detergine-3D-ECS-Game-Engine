dg_add_app(dg-player SOURCES player/Main.cpp DEPS dg::runtime)

if(DG_BUILD_SERVER)
    dg_add_app(dg-server SOURCES server/Main.cpp DEPS dg::runtime)
endif()

if(DG_BUILD_EDITOR)
    dg_add_app(dg-editor SOURCES editor/Main.cpp DEPS dg::runtime)
endif()
