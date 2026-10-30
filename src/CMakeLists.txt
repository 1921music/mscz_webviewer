add_library(mscz_core STATIC
    mscz_engine.cpp
    mscz_engine.h
    mscz_app_stub.h
)

target_include_directories(mscz_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${MU_ROOT}
    ${MU_ROOT}/src
    ${MU_ROOT}/src/framework
    ${MU_ROOT}/src/framework/global
    ${MU_ROOT}/src/framework/draw
    ${MU_ROOT}/src/engraving
)

target_link_libraries(mscz_core PUBLIC
    muse::global
    muse::draw
    engraving
    Qt6::Core
    Qt6::Gui
)

add_executable(mscz_probe
    mscz_probe.cpp
)

target_link_libraries(mscz_probe PRIVATE mscz_core)
