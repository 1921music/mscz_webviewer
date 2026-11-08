add_library(mscz_core STATIC
    mscz_engine.cpp
    mscz_engine.h
    displaytranspose.cpp
    displaytranspose.h
    textfontbackend.cpp
    textfontbackend.h
    musicxmlimport.cpp
    musicxmlimport.h
    mscz_app_stub.h
    svgpaintprovider.cpp
    svgpaintprovider.h
    glyphoutlines.cpp
    glyphoutlines.h
    fontsubset.cpp
    fontsubset.h
    hostio.cpp
    hostio.h
    compatfontprovider.cpp
    compatfontprovider.h
)
if(NOT MSCZ_PLAYBACK STREQUAL "OFF")
    target_sources(mscz_core PRIVATE playbacktimeline.cpp)
    target_compile_definitions(mscz_core PUBLIC MSCZ_ENABLE_PLAYBACK)
endif()

# MuseScore's two-pass MusicXML importer, without the writer, GUI or XSD
# validator (the desktop module also disables XSD validation).
set(MSCZ_MUSICXML_DIR "${MU_ROOT}/src/importexport/musicxml/internal")
add_library(mscz_musicxml STATIC)
file(GLOB MSCZ_MUSICXML_SOURCES CONFIGURE_DEPENDS
    "${MSCZ_MUSICXML_DIR}/import/*.cpp"
    "${MSCZ_MUSICXML_DIR}/shared/*.cpp")
target_sources(mscz_musicxml PRIVATE ${MSCZ_MUSICXML_SOURCES}
    "${MSCZ_MUSICXML_DIR}/musicxmlconfiguration.cpp")
target_compile_definitions(mscz_musicxml PRIVATE MUSICXML_NO_INTERACTIVE MUSICXML_NO_VALIDATION)
target_include_directories(mscz_musicxml PRIVATE
    ${CMAKE_BINARY_DIR} ${MU_ROOT} ${MU_ROOT}/src ${MU_ROOT}/src/framework
    ${MU_ROOT}/src/framework/global ${MU_ROOT}/src/engraving
    ${MU_ROOT}/src/importexport/musicxml)
target_link_libraries(mscz_musicxml PUBLIC engraving muse::global muse::draw)

target_include_directories(mscz_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${MU_ROOT}
    ${MU_ROOT}/src
    ${MU_ROOT}/src/framework
    ${MU_ROOT}/src/framework/global
    ${MU_ROOT}/src/framework/draw
    ${MU_ROOT}/src/engraving
)

if (MSCZ_VENDORED_THIRDPARTY)
    # Set up by cmake/MsczThirdparty.cmake, which the root list file includes
    # before the engine so the draw module picks up the same copies.
    set(MSCZ_FT_LIB ${FREETYPE_LIBRARIES})
    # The vendored HarfBuzz is one library covering both shaping and
    # subsetting; see cmake/MsczThirdparty.cmake for why it cannot be two.
    set(MSCZ_HB_SUBSET_LIB ${HARFBUZZ_LIBRARIES})
    target_include_directories(mscz_core PRIVATE ${FREETYPE_INCLUDE_DIRS})
else()
    find_package(Freetype REQUIRED)
    set(MSCZ_FT_LIB Freetype::Freetype)

    # hb-subset, for cutting fonts down to the glyphs a page uses.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HARFBUZZ_SUBSET REQUIRED IMPORTED_TARGET harfbuzz-subset)
    set(MSCZ_HB_SUBSET_LIB PkgConfig::HARFBUZZ_SUBSET)
endif()

target_link_libraries(mscz_core PUBLIC
    ${MSCZ_FT_LIB}
    ${MSCZ_HB_SUBSET_LIB}
    muse::global
    muse::draw
    engraving
    mscz_musicxml
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

if (EMSCRIPTEN)
    # The browser build. Plain C exports rather than embind, and no filesystem
    # of any kind: resources are pushed in from JS. See src/mscz_wasm.cpp.
    add_executable(mscz_wasm mscz_wasm.cpp)
    target_link_libraries(mscz_wasm PRIVATE mscz_core)
    # mscz-engine.js is emscripten's generated loader; web/mscz.js is the
    # hand-written wrapper over it, so the names must not collide.
    set_target_properties(mscz_wasm PROPERTIES OUTPUT_NAME mscz-engine)

    # Matching the compile side: a size build optimises at link too, a quick
    # build does not, because the link is 2% of the time either way and -Oz
    # -flto there is what turns 95 seconds into something longer.
    if (MSCZ_WASM_SIZE_BUILD)
        target_link_options(mscz_wasm PRIVATE -Oz)
        if (MSCZ_WASM_LTO)
            target_link_options(mscz_wasm PRIVATE -flto)
        else()
            # Without LTO the map is worth having: it says which input object
            # every output byte came from, which is the only exact answer to
            # "where does the four and a half megabytes go".
            target_link_options(mscz_wasm PRIVATE
                "-Wl,--Map=${CMAKE_BINARY_DIR}/mscz-engine.map"
            )
        endif()
    endif()

    target_link_options(mscz_wasm PRIVATE
        --no-entry
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createMscz
        # node is in here only so the build can be checked without a browser:
        # this distro has no Chromium for playwright, and the check has to run
        # somewhere. It costs a few KB of loader JS and nothing in the .wasm.
        -sENVIRONMENT=web,worker,node
        # Nothing opens a file, so there is no reason to carry a filesystem.
        -sFILESYSTEM=0
        # emmalloc is the smaller allocator and we make few, large allocations.
        -sMALLOC=emmalloc
        -sALLOW_MEMORY_GROWTH=1
        -sEXPORTED_RUNTIME_METHODS=cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8
        -sINCOMING_MODULE_JS_API=wasmBinary,locateFile,textFontBackend
        # A separate file mapping function index to mangled name. Nothing is
        # added to the .wasm -- -Oz strips the name section -- but without it a
        # size attribution can only say "function 7231".
        --emit-symbol-map
    )

    # One target, one artefact set: the .wasm travels with its loader.
    set_target_properties(mscz_wasm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
    )

    # The rest of what the page needs: the wrapper and the demo, and the
    # resources laid out as files with an index saying what each is called.
    # Nothing is baked into the wasm, which is the point -- what gets fetched
    # is then a delivery decision rather than a build one.
    set(MSCZ_WEB_DIR "${CMAKE_BINARY_DIR}/web")

    # Select/copy assets at configure time. Track inputs so rebuilding after
    # a page, sound bank or notice edit also regenerates the standalone HTML.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/scripts/gen-web-ui.py"
        "${CMAKE_SOURCE_DIR}/web/mscz.js" "${CMAKE_SOURCE_DIR}/web/text-fonts.js"
        "${CMAKE_SOURCE_DIR}/web/index.html" "${CMAKE_SOURCE_DIR}/web/playback.js"
        "${CMAKE_SOURCE_DIR}/web/playback-worker.js" "${CMAKE_SOURCE_DIR}/web/playback-ui.js"
        "${CMAKE_SOURCE_DIR}/assets/playback/sonivox.sf3"
        "${CMAKE_SOURCE_DIR}/assets/playback/LICENSE" "${CMAKE_SOURCE_DIR}/assets/playback/UPSTREAM.md"
        "${CMAKE_SOURCE_DIR}/vendor/sonivox/LICENSE" "${CMAKE_SOURCE_DIR}/vendor/tinysoundfont/LICENSE"
        "${CMAKE_SOURCE_DIR}/vendor/stb/stb_vorbis.c")
    execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/gen-web-ui.py"
        --web "${MSCZ_WEB_DIR}" --backend "${MSCZ_PLAYBACK}" COMMAND_ERROR_IS_FATAL ANY)
    set(MSCZ_PLAYBACK_DEPENDS "")
    if(NOT MSCZ_PLAYBACK STREQUAL "OFF")
        include("${CMAKE_SOURCE_DIR}/cmake/MsczPlayback.cmake")
        set(MSCZ_PLAYBACK_DEPENDS mscz_synth "${MSCZ_WEB_DIR}/playback.js"
            "${MSCZ_WEB_DIR}/playback-worker.js" "${MSCZ_WEB_DIR}/playback-ui.js")
    endif()

    add_custom_command(TARGET mscz_wasm POST_BUILD
        COMMAND ${Python3_EXECUTABLE} -I
                "${CMAKE_CURRENT_LIST_DIR}/../scripts/gen-web-resources.py"
                --manifest "${MSCZ_RESOURCE_MANIFEST}"
                --root "${MU_ROOT}"
                --out "${MSCZ_WEB_DIR}"
        COMMENT "Laying out the web payload in ${MSCZ_WEB_DIR}"
        VERBATIM
    )
    add_dependencies(mscz_wasm mscz_resource_manifest)

    # Also deliver a self-contained file:// page. Separate output dependencies
    # let page/wrapper edits repackage it even when the engine needn't relink.
    add_custom_command(
        OUTPUT "${MSCZ_WEB_DIR}/index-standalone.html"
        COMMAND ${Python3_EXECUTABLE} -I
                "${CMAKE_CURRENT_LIST_DIR}/../scripts/gen-web-resources.py"
                --manifest "${MSCZ_RESOURCE_MANIFEST}"
                --root "${MU_ROOT}" --out "${MSCZ_WEB_DIR}"
        COMMAND ${Python3_EXECUTABLE} -I
                "${CMAKE_CURRENT_LIST_DIR}/../scripts/gen-standalone-html.py"
                --web "${MSCZ_WEB_DIR}"
        DEPENDS mscz_wasm mscz_resource_manifest ${MSCZ_PLAYBACK_DEPENDS}
                "${MSCZ_RESOURCE_MANIFEST}"
                "${MSCZ_WEB_DIR}/index.html" "${MSCZ_WEB_DIR}/mscz.js"
                "${MSCZ_WEB_DIR}/text-fonts.js"
                "${CMAKE_CURRENT_LIST_DIR}/../scripts/gen-web-resources.py"
                "${CMAKE_CURRENT_LIST_DIR}/../scripts/gen-standalone-html.py"
        COMMENT "Embedding JavaScript, wasm and all resources in index-standalone.html"
        VERBATIM
    )
    add_custom_target(mscz_standalone ALL DEPENDS "${MSCZ_WEB_DIR}/index-standalone.html")
endif()

# The native probe opens files directly and takes a command line, neither of
# which the browser build has.
if (NOT EMSCRIPTEN)
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

# Explicit regression target: not part of the shipping binaries.
add_executable(mscz_musicxml_test EXCLUDE_FROM_ALL ../test/musicxml.cpp)
add_dependencies(mscz_musicxml_test mscz_resource_manifest)
target_link_libraries(mscz_musicxml_test PRIVATE mscz_core)
target_compile_definitions(mscz_musicxml_test PRIVATE
    MSCZ_RESOURCE_ROOT="${MU_ROOT}"
    MSCZ_RESOURCE_MANIFEST="${MSCZ_RESOURCE_MANIFEST}"
)

add_executable(mscz_svg_symbols_test EXCLUDE_FROM_ALL ../test/svg_symbols.cpp)
add_dependencies(mscz_svg_symbols_test mscz_resource_manifest)
target_link_libraries(mscz_svg_symbols_test PRIVATE mscz_core)
target_compile_definitions(mscz_svg_symbols_test PRIVATE
    MSCZ_RESOURCE_ROOT="${MU_ROOT}"
    MSCZ_RESOURCE_MANIFEST="${MSCZ_RESOURCE_MANIFEST}"
)

add_executable(mscz_transpose_test EXCLUDE_FROM_ALL ../test/transpose.cpp)
add_dependencies(mscz_transpose_test mscz_resource_manifest)
target_link_libraries(mscz_transpose_test PRIVATE mscz_core)
target_compile_definitions(mscz_transpose_test PRIVATE
    MSCZ_RESOURCE_ROOT="${MU_ROOT}"
    MSCZ_RESOURCE_MANIFEST="${MSCZ_RESOURCE_MANIFEST}"
)

add_executable(mscz_spelling_test EXCLUDE_FROM_ALL ../test/spelling.cpp)
add_dependencies(mscz_spelling_test mscz_resource_manifest)
target_link_libraries(mscz_spelling_test PRIVATE mscz_core)
target_compile_definitions(mscz_spelling_test PRIVATE
    MSCZ_RESOURCE_ROOT="${MU_ROOT}"
    MSCZ_RESOURCE_MANIFEST="${MSCZ_RESOURCE_MANIFEST}"
    MSCZ_SPELLING_FIXTURE="${CMAKE_CURRENT_LIST_DIR}/../test/spelling.mscx"
)

add_executable(mscz_style_compat_test EXCLUDE_FROM_ALL ../test/style_compat.cpp)
add_dependencies(mscz_style_compat_test mscz_resource_manifest)
target_link_libraries(mscz_style_compat_test PRIVATE mscz_core)
target_compile_definitions(mscz_style_compat_test PRIVATE
    MSCZ_RESOURCE_ROOT="${MU_ROOT}"
    MSCZ_RESOURCE_MANIFEST="${MSCZ_RESOURCE_MANIFEST}"
    MSCZ_STYLE_COMPAT_FIXTURE="${CMAKE_CURRENT_LIST_DIR}/../test/browser_text.mscx"
)

if (MUSE_QT_SUPPORT)
    # Both providers exist only in the Qt build, which is the one place the
    # port can be checked against what it was ported from.
    add_executable(mscz_metriccmp mscz_metriccmp.cpp)
    add_dependencies(mscz_metriccmp mscz_resource_manifest)
    target_link_libraries(mscz_metriccmp PRIVATE mscz_core)
    target_compile_definitions(mscz_metriccmp PRIVATE
        MSCZ_RESOURCE_ROOT="${MU_ROOT}"
        MSCZ_RESOURCE_MANIFEST="${MSCZ_RESOURCE_MANIFEST}"
    )
endif()
endif() # NOT EMSCRIPTEN

