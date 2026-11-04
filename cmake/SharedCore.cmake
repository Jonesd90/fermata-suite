# The shared core: the code in src/core that BOTH programs, fermata and fermata:Live, are built from (see COMPATIBILITY.md).
# This is the one list of its .cpp files - each program adds ${FERMATA_SHARED_SOURCES} to its own sources, so a file added here is in both builds.
# (Its headers need no list: the .cpp files and the programs' own files include them.)
get_filename_component(FERMATA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(FERMATA_SHARED_SOURCES
    ${FERMATA_ROOT}/src/core/Project.cpp
    ${FERMATA_ROOT}/src/core/AudioEngine.cpp
    ${FERMATA_ROOT}/src/core/Playback.cpp
    ${FERMATA_ROOT}/src/core/Bounce.cpp
    ${FERMATA_ROOT}/src/core/AudioOps.cpp
    ${FERMATA_ROOT}/src/core/DenoiseFft.cpp
    ${FERMATA_ROOT}/src/core/ImportPlan.cpp
    ${FERMATA_ROOT}/src/core/Aaf.cpp
    ${FERMATA_ROOT}/src/core/AafWrite.cpp
    ${FERMATA_ROOT}/src/core/AafExport.cpp
    ${FERMATA_ROOT}/src/core/TakeMatch.cpp
    ${FERMATA_ROOT}/src/core/Ltc.cpp
    ${FERMATA_ROOT}/src/core/MasterDef.cpp
    ${FERMATA_ROOT}/src/core/Ddp.cpp
    ${FERMATA_ROOT}/src/core/MasterExport.cpp
    ${FERMATA_ROOT}/src/core/ReviewPage.cpp
    ${FERMATA_ROOT}/src/core/MasterRender.cpp
    ${FERMATA_ROOT}/src/core/Ravenna.cpp
    ${FERMATA_ROOT}/src/core/CoreReservation.cpp
    ${FERMATA_ROOT}/src/core/CrashReport.cpp
    ${FERMATA_ROOT}/src/core/Retime.cpp
    ${FERMATA_ROOT}/src/core/VideoExport.cpp)

# The JUCE settings both programs are built with (each program adds its own name and version on top). The same for both, so the shared
# code behaves the same in each.
set(FERMATA_SHARED_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(FERMATA_SHARED_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# The audio drivers on Windows, for both programs: ASIO (the Steinberg SDK, fetched by the GitHub build) and WASAPI. fermata_use_windows_audio (target).
function(fermata_use_windows_audio target)
    if(WIN32)
        if(NOT ASIO_SDK_DIR OR NOT EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
            message(FATAL_ERROR "Set ASIO_SDK_DIR to the Steinberg ASIO SDK folder (it must contain common/iasiodrv.h)")
        endif()
        target_compile_definitions(${target} PRIVATE JUCE_ASIO=1 JUCE_WASAPI=1 JUCE_DIRECTSOUND=0)
        target_include_directories(${target} PRIVATE
            "${ASIO_SDK_DIR}/common" "${ASIO_SDK_DIR}/host" "${ASIO_SDK_DIR}/host/pc")
    endif()
endfunction()

# Debugging symbols on Windows so a crash log can show exactly where it happened; the .pdb is copied next to the exe in dist/<folder>.
# fermata_keep_debug_symbols (target folder).
function(fermata_keep_debug_symbols target folder)
    if(MSVC)
        target_compile_options(${target} PRIVATE /Zi)
        target_link_options(${target} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist/${folder}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${target}>" "${CMAKE_SOURCE_DIR}/dist/${folder}/")
    endif()
endfunction()
