# Separate, small synth module: it lives in a Web Worker, not the layout thread.
if(MSCZ_PLAYBACK STREQUAL "EAS")
    set(eas "${CMAKE_SOURCE_DIR}/vendor/sonivox/arm-wt-22k")
    add_executable(mscz_synth "${CMAKE_SOURCE_DIR}/src/synth/eas.c")
    foreach(file eas_flog eas_math eas_midi eas_mixbuf eas_mixer eas_pan eas_pcm
                 eas_public eas_tonecontrol eas_voicemgt eas_sndlibmgt eas_wtengine eas_wtsynth eas_filter wt_200k_G)
        target_sources(mscz_synth PRIVATE "${eas}/lib_src/${file}.c")
    endforeach()
    target_sources(mscz_synth PRIVATE "${eas}/host_src/eas_config.c" "${eas}/host_src/eas_report.c" "${eas}/src/hostmm_ng.c")
    target_include_directories(mscz_synth PRIVATE "${CMAKE_SOURCE_DIR}/src/synth" "${eas}/host_src" "${eas}/lib_src" "${CMAKE_SOURCE_DIR}/vendor/sonivox/fakes")
else()
    add_executable(mscz_synth "${CMAKE_SOURCE_DIR}/src/synth/tsf.c")
    target_include_directories(mscz_synth PRIVATE "${CMAKE_SOURCE_DIR}/vendor/tinysoundfont" "${CMAKE_SOURCE_DIR}/vendor/stb")
endif()
target_compile_options(mscz_synth PRIVATE -Oz -flto)
target_compile_definitions(mscz_synth PRIVATE NDEBUG)
target_link_options(mscz_synth PRIVATE -Oz -flto --no-entry
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createSynth
    -sENVIRONMENT=worker,node -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=0
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP16
    -sINCOMING_MODULE_JS_API=wasmBinary,locateFile)
set_target_properties(mscz_synth PROPERTIES OUTPUT_NAME mscz-synth RUNTIME_OUTPUT_DIRECTORY "${MSCZ_WEB_DIR}")
