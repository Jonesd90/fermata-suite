# fermata:Live - the live-concert recorder, built from the same shared core as fermata (see COMPATIBILITY.md and docs/LIVE-BRIEF.md).
# Included by the top-level CMakeLists.txt; this file holds everything that is fermata:Live's own, so its work never edits fermata's lines.
set(FERMATA_LIVE_VERSION 0.10.2)    # fermata:Live's version: the ONE place it is changed (see LIVE-CHANGELOG.md)

# The program is called "fermata:Live" on screen. Windows allows no colon in a file name, so the file is fermata-Live.exe.
juce_add_gui_app(FermataLive
    PRODUCT_NAME "fermata-Live"
    COMPANY_NAME "Fermata"
    VERSION "${FERMATA_LIVE_VERSION}"
    ICON_BIG "${FERMATA_ROOT}/assets/fermata-live-icon.png"
    ICON_SMALL "${FERMATA_ROOT}/assets/fermata-live-icon.png")

target_sources(FermataLive PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/Main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/DriveWriter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/LiveRecorder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Merge.cpp
    ${CMAKE_CURRENT_LIST_DIR}/RecorderService.cpp
    ${CMAKE_CURRENT_LIST_DIR}/RecorderLink.cpp
    ${CMAKE_CURRENT_LIST_DIR}/RecorderWindow.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MonitorMix.cpp
    ${CMAKE_CURRENT_LIST_DIR}/StreamServer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/StationModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/LiveTakeView.cpp
    ${CMAKE_CURRENT_LIST_DIR}/LiveMixer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PrimaryApp.cpp
    ${CMAKE_CURRENT_LIST_DIR}/LiveMainWindow.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PrimaryTakeWindow.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PrimaryWindows.cpp
    ${CMAKE_CURRENT_LIST_DIR}/FixerApp.cpp
    ${CMAKE_CURRENT_LIST_DIR}/FixerWindow.cpp
    ${CMAKE_CURRENT_LIST_DIR}/SecondSeat.cpp
    ${FERMATA_SHARED_SOURCES}
    # fermata's look, shared (COMPATIBILITY.md, contract 3): its fonts and the window helpers it uses
    ${FERMATA_ROOT}/src/ui/FontData.cpp
    ${FERMATA_ROOT}/src/ui/FontData2.cpp
    ${FERMATA_ROOT}/src/ui/WinOwner.cpp)

# FERMATA_VERSION_STRING is what the shared code writes as the program's version (AudioEngine stamps "Fermata <it>" into recorded files):
# "Live 0.3.0" makes that "Fermata Live 0.3.0". The windows show FERMATA_LIVE_VERSION_STRING.
target_compile_definitions(FermataLive PRIVATE
    ${FERMATA_SHARED_DEFINITIONS}
    FERMATA_LIVE=1
    FERMATA_LIVE_VERSION_STRING="${FERMATA_LIVE_VERSION}"
    FERMATA_VERSION_STRING="Live ${FERMATA_LIVE_VERSION}")

fermata_keep_debug_symbols(FermataLive fermata-Live)
fermata_use_windows_audio(FermataLive)

target_link_libraries(FermataLive PRIVATE ${FERMATA_SHARED_JUCE_MODULES})
