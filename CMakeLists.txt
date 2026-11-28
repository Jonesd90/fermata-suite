cmake_minimum_required(VERSION 3.22)
set(FERMATA_VERSION 3.74.0)        # fermata's version: the ONE place it is changed (see CHANGELOG.md). fermata:Live's is in src/live/Live.cmake.
project(Fermata VERSION ${FERMATA_VERSION} LANGUAGES C CXX)

# This repository builds TWO programs from one shared core (read COMPATIBILITY.md before changing anything shared):
#   fermata        this file (its own code: src/Main.cpp, src/ui, src/reharmoniser)
#   fermata:Live   src/live/Live.cmake (its own code: src/live)
#   shared         src/core, listed once in cmake/SharedCore.cmake

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime on Windows so the .exe runs on any PC without installing the Visual C++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Path to the Steinberg ASIO SDK (the folder that contains "common", "host" and "driver").
set(ASIO_SDK_DIR "" CACHE PATH "Steinberg ASIO SDK folder")

include(cmake/SharedCore.cmake)

# ---------------------------------------------------------------------------------------------------------------------------------------------
# fermata
juce_add_gui_app(Fermata
    PRODUCT_NAME "Fermata"
    COMPANY_NAME "Fermata"
    VERSION "${FERMATA_VERSION}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/fermata-icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/fermata-icon.png")

target_sources(Fermata PRIVATE
    src/Main.cpp
    ${FERMATA_SHARED_SOURCES}
    src/reharmoniser/Dsp.cpp
    src/reharmoniser/Engine.cpp
    src/reharmoniser/Spectrogram.cpp
    src/reharmoniser/ReHarmoniserEditor.cpp
    src/ui/AppContext.cpp
    src/ui/BounceWindow.cpp
    src/ui/DesignWindow.cpp
    src/ui/EditWindow.cpp
    src/ui/FixTools.cpp
    src/ui/FixToolsEdits.cpp
    src/ui/MasteringWindow.cpp
    src/ui/SpecEditWindow.cpp          # 3.74.0: the Spectrogram Editing window (the piece's figures and meters)
    src/ui/FontData.cpp
    src/ui/FontData2.cpp
    src/ui/MainWindow.cpp
    src/ui/MediaWindow.cpp
    src/ui/WinOwner.cpp
    src/ui/TakeDisplay.cpp
    src/ui/ScopeWindow.cpp
    src/ui/RetimerWindow.cpp
    src/ui/RxLink.cpp
    src/ui/OrganiserWindow.cpp
    src/ui/PitchPipeWindow.cpp
    src/ui/AlignWindow.cpp
    src/ui/MixerWindow.cpp
    src/ui/PluginHost.cpp
    src/ui/RemoteControl.cpp
    src/ui/Surface.cpp
    src/ui/TakeWindow.cpp
    src/ui/TrimWindow.cpp
    src/ui/Video.cpp                   # 3.68.0: the edit's video: the lane's and the picture window's side of it,
    src/ui/VideoSource.cpp)            #         and the decoder's thread (every system). The decoder itself: FermataVideoReader, below

target_compile_definitions(Fermata PRIVATE
    ${FERMATA_SHARED_DEFINITIONS}
    FERMATA_VERSION_STRING="${FERMATA_VERSION}")

# 3.51.0: the score window's PDF pages (src/ui/PdfPages.h). They are drawn by Windows' own PDF reader, reached through C++/WinRT - headers that
# come with the Windows SDK and that nothing else here uses. That one file is built on its own, as C++20, and ONLY IF it builds here: it is
# tried first (compiled and linked into a small program). If the trial fails - an SDK without those headers, or a mistake in that file, which
# could not be compiled where it was written - fermata is built without it (PdfPagesNone.cpp: the score window then says that this build
# cannot draw the pages) and the reason is printed below as a warning. The PDF reader can never stop fermata from building.
set(FERMATA_PDF_SOURCE src/ui/PdfPagesNone.cpp)
if(MSVC)
    try_compile(FERMATA_PDF_READER_BUILDS "${CMAKE_BINARY_DIR}/pdf-reader-trial"
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PdfPagesWin.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PdfCheckMain.cpp"
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        OUTPUT_VARIABLE FERMATA_PDF_READER_LOG)
    if(FERMATA_PDF_READER_BUILDS)
        set(FERMATA_PDF_SOURCE src/ui/PdfPagesWin.cpp)
        message(STATUS "PDF pages: the reader is in (Windows.Data.Pdf)")
    else()
        message(WARNING "PDF pages: the reader did NOT build, so fermata is built without it (the score window will say so). The trial's output:\n${FERMATA_PDF_READER_LOG}")
    endif()
endif()
# 3.68.0: the video reader (src/ui/VideoReader.h): this computer's own decoder behind fermata's video. On Windows that is Media Foundation,
# src/ui/VideoReaderWin.cpp - which, like the PDF reader above, could not be compiled where it was written. So it is tried on its own first
# (compiled and linked into a small program): as it is, with the graphics card's decoder; if that does not build, without it
# (FERMATA_VIDEO_D3D=0); and if that does not build either, fermata is built with VideoReaderNone.cpp (the picture window then says this
# build cannot read video) and the reasons are printed below as a warning and written to video-reader-did-not-build.txt in the build
# folder, which the GitHub build puts beside Fermata.exe. The video reader can never stop fermata from building.
set(FERMATA_VIDEO_SOURCE src/ui/VideoReaderNone.cpp)
set(FERMATA_VIDEO_D3D 1)
set(FERMATA_VIDEO_TRIALS "not tried (the compiler is not MSVC)")
file(REMOVE "${CMAKE_BINARY_DIR}/video-reader-did-not-build.txt")
if(MSVC)
    set(FERMATA_VIDEO_TRIALS "tried")
    try_compile(FERMATA_VIDEO_READER_BUILDS "${CMAKE_BINARY_DIR}/video-reader-trial"
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/VideoReaderWin.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VideoCheckMain.cpp"
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        OUTPUT_VARIABLE FERMATA_VIDEO_READER_LOG)
    if(FERMATA_VIDEO_READER_BUILDS)
        set(FERMATA_VIDEO_SOURCE src/ui/VideoReaderWin.cpp)
        message(STATUS "Video: the reader is in (Media Foundation, with the graphics card's decoder)")
        set(FERMATA_VIDEO_TRIALS "tried: the first trial (with the graphics card's decoder) built")
    else()
        try_compile(FERMATA_VIDEO_READER_BUILDS_PLAIN "${CMAKE_BINARY_DIR}/video-reader-trial-plain"
            SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/VideoReaderWin.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VideoCheckMain.cpp"
            COMPILE_DEFINITIONS -DFERMATA_VIDEO_D3D=0
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
            OUTPUT_VARIABLE FERMATA_VIDEO_READER_LOG_PLAIN)
        if(FERMATA_VIDEO_READER_BUILDS_PLAIN)
            set(FERMATA_VIDEO_SOURCE src/ui/VideoReaderWin.cpp)
            set(FERMATA_VIDEO_D3D 0)
            set(FERMATA_VIDEO_TRIALS "tried: the first trial failed; the second (without the graphics card's decoder) built")
            message(WARNING "Video: the reader is in WITHOUT the graphics card's decoder (that part did not build: the processor decodes). The first trial's output:\n${FERMATA_VIDEO_READER_LOG}")
            file(WRITE "${CMAKE_BINARY_DIR}/video-reader-did-not-build.txt" "fermata's video reader was built WITHOUT the graphics card's decoder: that part did not compile. Video still works (the processor decodes). Please send this file with your next message.\n\n${FERMATA_VIDEO_READER_LOG}")
        else()
            set(FERMATA_VIDEO_TRIALS "tried: BOTH trials failed - fermata is built without video")
            message(WARNING "Video: the reader did NOT build, so fermata is built without it (the picture window will say so). The trials' output:\n${FERMATA_VIDEO_READER_LOG}\n---- without the graphics card's decoder:\n${FERMATA_VIDEO_READER_LOG_PLAIN}")
            file(WRITE "${CMAKE_BINARY_DIR}/video-reader-did-not-build.txt" "fermata's video reader did NOT compile, so this build cannot show video. Please send this file with your next message.\n\n${FERMATA_VIDEO_READER_LOG}\n\n---- without the graphics card's decoder:\n${FERMATA_VIDEO_READER_LOG_PLAIN}")
        endif()
    endif()
endif()
# 3.73.0 (27 November 2026: the video reader was left out of the user's build, and the file above was not in the download either - the
# reason could not be seen): video-reader.txt is now ALWAYS written - what was tried, with what, what came of it, and every trial's output -
# and put beside Fermata.exe by the GitHub build. It is also built INTO fermata (generated/VideoBuildReport.h, read by src/ui/VideoSource.cpp):
# Help > Save the video report writes it out again, so it can be sent even when the download has no such file.
set(FERMATA_VIDEO_REPORT "fermata ${FERMATA_VERSION}: how its video reader was built\n\n")
string(APPEND FERMATA_VIDEO_REPORT "The reader built in: ${FERMATA_VIDEO_SOURCE}  (graphics card's decoder: ${FERMATA_VIDEO_D3D})\n")
string(APPEND FERMATA_VIDEO_REPORT "The trials: ${FERMATA_VIDEO_TRIALS}\n")
string(APPEND FERMATA_VIDEO_REPORT "CMake ${CMAKE_VERSION}; generator: ${CMAKE_GENERATOR} ${CMAKE_GENERATOR_PLATFORM}; system: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}\n")
string(APPEND FERMATA_VIDEO_REPORT "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER}); MSVC: ${MSVC}\n")
string(APPEND FERMATA_VIDEO_REPORT "Windows SDK: ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}\n")
macro(fermata_video_report_log var title)
    if(DEFINED ${var})
        set(log "${${var}}")
        string(LENGTH "${log}" len)
        if(len GREATER 60000)                                                   # (the end of a long log is where the errors are)
            math(EXPR from "${len} - 60000")
            string(SUBSTRING "${log}" ${from} 60000 log)
            set(log "(... the first part left out ...)\n${log}")
        endif()
        string(APPEND FERMATA_VIDEO_REPORT "\n==== ${title}: its output\n${log}\n")
    endif()
endmacro()
fermata_video_report_log(FERMATA_VIDEO_READER_LOG "the first trial (with the graphics card's decoder)")
fermata_video_report_log(FERMATA_VIDEO_READER_LOG_PLAIN "the second trial (without it)")
file(WRITE "${CMAKE_BINARY_DIR}/video-reader.txt" "${FERMATA_VIDEO_REPORT}")
file(READ "${CMAKE_BINARY_DIR}/video-reader.txt" FERMATA_VIDEO_REPORT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FERMATA_VIDEO_REPORT_HEX "${FERMATA_VIDEO_REPORT_HEX}")
file(WRITE "${CMAKE_BINARY_DIR}/generated/VideoBuildReport.h"
     "// made by CMakeLists.txt (3.73.0): build/video-reader.txt, as bytes\nstatic const unsigned char kVideoBuildReport[] = { ${FERMATA_VIDEO_REPORT_HEX} 0x00 };\n")
target_include_directories(Fermata PRIVATE "${CMAKE_BINARY_DIR}/generated")

add_library(FermataVideoReader STATIC ${FERMATA_VIDEO_SOURCE})
set_target_properties(FermataVideoReader PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(FermataVideoReader PRIVATE FERMATA_VIDEO_D3D=${FERMATA_VIDEO_D3D})
target_link_libraries(Fermata PRIVATE FermataVideoReader)

add_library(FermataPdfPages STATIC ${FERMATA_PDF_SOURCE})
set_target_properties(FermataPdfPages PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(Fermata PRIVATE FermataPdfPages)

fermata_keep_debug_symbols(Fermata Fermata)
fermata_use_windows_audio(Fermata)

target_link_libraries(Fermata PRIVATE ${FERMATA_SHARED_JUCE_MODULES})

# ---------------------------------------------------------------------------------------------------------------------------------------------
# fermata:Live (its own file, so work on one program does not touch the other's build lines)
include(src/live/Live.cmake)
