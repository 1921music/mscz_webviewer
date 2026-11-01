add_library(mscz_core STATIC
    mscz_engine.cpp
    mscz_engine.h
    mscz_app_stub.h
    svgpaintprovider.cpp
    svgpaintprovider.h
    glyphoutlines.cpp
    glyphoutlines.h
    fontsubset.cpp
    fontsubset.h
    hostio.cpp
    hostio.h
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

find_package(Freetype REQUIRED)

# hb-subset, for cutting fonts down to the glyphs a page uses. The wasm build
# will use the vendored HarfBuzz amalgam instead; see vendor/thirdparty.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HARFBUZZ_SUBSET REQUIRED IMPORTED_TARGET harfbuzz-subset)

target_link_libraries(mscz_core PUBLIC
    Freetype::Freetype
    PkgConfig::HARFBUZZ_SUBSET
    muse::global
    muse::draw
    engraving
)

if (MUSE_QT_SUPPORT)
    # Only the PNG path and the application object need these.
    target_link_libraries(mscz_core PUBLIC Qt6::Core Qt6::Gui)
endif()

# The engine asks for resources by Qt resource alias -- ":/engraving/
# instruments/instruments.xml" is really share/instruments/instruments.xml --
# and without Qt there is no compiled-in table to resolve those. Generate the
# same mapping from the .qrc files so the host can, and so it cannot drift.
file(GLOB MSCZ_QRC_FILES
    "${MU_ROOT}/src/engraving/engraving.qrc"
    "${MU_ROOT}/src/engraving/data/fonts/*.qrc"
)
set(MSCZ_RESOURCE_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/resources.manifest")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
    OUTPUT "${MSCZ_RESOURCE_MANIFEST}"
    COMMAND ${Python3_EXECUTABLE} -I
            "${CMAKE_CURRENT_LIST_DIR}/../scripts/gen-resource-manifest.py"
            --out "${MSCZ_RESOURCE_MANIFEST}"
            --root "${MU_ROOT}"
            ${MSCZ_QRC_FILES}
    DEPENDS ${MSCZ_QRC_FILES} "${CMAKE_CURRENT_LIST_DIR}/../scripts/gen-resource-manifest.py"
    COMMENT "Generating the resource manifest from the .qrc files"
    VERBATIM
)
add_custom_target(mscz_resource_manifest DEPENDS "${MSCZ_RESOURCE_MANIFEST}")

add_executable(mscz_probe
    mscz_probe.cpp
)

add_dependencies(mscz_probe mscz_resource_manifest)

# Defaults for the probe. The engine never learns either of these; it only ever
# asks the provider by name.
target_compile_definitions(mscz_probe PRIVATE
    MSCZ_RESOURCE_ROOT="${MU_ROOT}"
    MSCZ_RESOURCE_MANIFEST="${MSCZ_RESOURCE_MANIFEST}"
)

target_link_libraries(mscz_probe PRIVATE mscz_core)

