cmake_minimum_required(VERSION 3.21)
# The version lives in src/ui/main.cpp (setApplicationVersion); everything else reads it from there.
# Alpha versions are written "a.7.1.1": the "a" stands for major version 0 (0.7.1.1).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main.cpp" _lumora_version_line
     REGEX "setApplicationVersion\\(\"[0-9a-z.]+\"\\)")
string(REGEX MATCH "\"[0-9a-z.]+\"" LUMORA_DISPLAY_VERSION "${_lumora_version_line}")
string(REPLACE "\"" "" LUMORA_DISPLAY_VERSION "${LUMORA_DISPLAY_VERSION}")
string(REGEX REPLACE "^[a-z]+\\." "0." LUMORA_VERSION "${LUMORA_DISPLAY_VERSION}")
project(Lumora VERSION ${LUMORA_VERSION} LANGUAGES CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
  add_compile_options(/utf-8)   # sources contain UTF-8 symbols like ‹ › ⌫
endif()

option(LUMORA_BUILD_APP "Build the desktop application" ON)
option(LUMORA_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network SerialPort)
if(LUMORA_BUILD_APP)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Svg)
endif()

# ---------------------------------------------------------------------------
# RtMidi (vendored, MIT-style license) — picks the native MIDI API per platform
# ---------------------------------------------------------------------------
add_library(rtmidi STATIC third_party/rtmidi/RtMidi.cpp)
target_include_directories(rtmidi PUBLIC third_party/rtmidi)
if(WIN32)
  target_compile_definitions(rtmidi PUBLIC __WINDOWS_MM__)
  target_link_libraries(rtmidi PUBLIC winmm)
elseif(APPLE)
  target_compile_definitions(rtmidi PUBLIC __MACOSX_CORE__)
  target_link_libraries(rtmidi PUBLIC "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
else()
  find_package(ALSA)
  if(ALSA_FOUND)
    target_compile_definitions(rtmidi PUBLIC __LINUX_ALSA__)
    target_link_libraries(rtmidi PUBLIC ALSA::ALSA pthread)
  else()
    message(WARNING "ALSA not found: MIDI will be disabled (install libasound2-dev)")
    target_compile_definitions(rtmidi PUBLIC __RTMIDI_DUMMY__)
  endif()
endif()

# ---------------------------------------------------------------------------
# Core: show model, engine, outputs, MIDI, command line (no GUI dependency)
# ---------------------------------------------------------------------------
add_library(lumora_core STATIC
  src/core/types.h src/core/types.cpp
  src/core/fixturetype.h src/core/fixturetype.cpp
  src/core/model.h src/core/model.cpp
  src/core/show.h src/core/show.cpp
  src/core/engine.h src/core/engine.cpp
  src/core/dmxoutput.h src/core/dmxoutput.cpp
  src/core/midi.h src/core/midi.cpp
  src/core/commandline.h src/core/commandline.cpp
  src/core/zipreader.h src/core/zipreader.cpp
  src/core/gdtf.cpp
  src/core/qxf.cpp
  src/core/parameters.h src/core/parameters.cpp
  src/core/importhelpers.h
  src/core/effects.h src/core/effects.cpp
  src/core/macros.h src/core/macros.cpp
  src/core/plots.h src/core/plots.cpp
)
target_include_directories(lumora_core PUBLIC src/core)
target_include_directories(lumora_core PRIVATE third_party/stb)
target_link_libraries(lumora_core PUBLIC Qt6::Core Qt6::Network Qt6::SerialPort rtmidi)
target_compile_definitions(lumora_core PUBLIC LUMORA_SOURCE_FIXTURES="${CMAKE_SOURCE_DIR}/fixtures" LUMORA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(lumora_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Desktop application
# ---------------------------------------------------------------------------
if(LUMORA_BUILD_APP)
  add_executable(lumora WIN32 MACOSX_BUNDLE
    src/ui/main.cpp
    src/ui/appcontext.h src/ui/appcontext.cpp src/ui/shortcuts.h
    src/ui/theme.h src/ui/theme.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/executorbar.h src/ui/executorbar.cpp
    src/ui/poolgrid.h src/ui/poolgrid.cpp
    src/ui/fixturesheet.h src/ui/fixturesheet.cpp
    src/ui/parkview.h src/ui/parkview.cpp
    src/ui/encoderbar.h src/ui/encoderbar.cpp
    src/ui/cueview.h src/ui/cueview.cpp
    src/ui/patchview.h src/ui/patchview.cpp
    src/ui/outputview.h src/ui/outputview.cpp
    src/ui/midiview.h src/ui/midiview.cpp
    src/ui/commandbar.h src/ui/commandbar.cpp
    src/ui/colorpicker.h src/ui/colorpicker.cpp
    src/ui/workspace.h src/ui/workspace.cpp src/ui/paneloptions.h
    src/ui/keypad.h src/ui/keypad.cpp
    src/ui/media.h src/ui/media.cpp
    src/ui/effectsui.h src/ui/effectsui.cpp
    src/ui/librarybrowser.h src/ui/librarybrowser.cpp
    src/ui/quickedit.h src/ui/quickedit.cpp
    src/ui/fixturesearch.h src/ui/fixturesearch.cpp
    src/ui/fixtureeditor.h src/ui/fixtureeditor.cpp
    src/ui/historyview.h src/ui/historyview.cpp
    src/ui/macrosui.h src/ui/macrosui.cpp
    src/ui/plotsui.h src/ui/plotsui.cpp
    src/ui/playbackmonitor.h src/ui/playbackmonitor.cpp
    src/ui/credentials.h src/ui/credentials.cpp
    src/ui/uiutil.h src/ui/uiutil.cpp
  )
  qt_add_resources(lumora "library" PREFIX "/library" BASE resources/library
    FILES resources/library/ofl-index.json resources/library/qlcplus-index.json)
  qt_add_resources(lumora "icons" PREFIX "/icons" BASE resources FILES resources/lumora.png)
  if(WIN32)
    # Program icon and the version shown in the file's Properties
    set(_lumora_rc_version "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR}")
    foreach(_part PATCH TWEAK)
      if("${PROJECT_VERSION_${_part}}" STREQUAL "")
        string(APPEND _lumora_rc_version ",0")
      else()
        string(APPEND _lumora_rc_version ",${PROJECT_VERSION_${_part}}")
      endif()
    endforeach()
    file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/resources/lumora.ico" _lumora_ico)
    file(WRITE ${CMAKE_BINARY_DIR}/lumora.rc
"IDI_ICON1 ICON \"${_lumora_ico}\"
1 VERSIONINFO
FILEVERSION ${_lumora_rc_version}
PRODUCTVERSION ${_lumora_rc_version}
BEGIN
  BLOCK \"StringFileInfo\"
  BEGIN
    BLOCK \"040904b0\"
    BEGIN
      VALUE \"CompanyName\", \"Lumora\"
      VALUE \"FileDescription\", \"Lumora lighting console\"
      VALUE \"FileVersion\", \"${LUMORA_DISPLAY_VERSION}\"
      VALUE \"ProductName\", \"Lumora\"
      VALUE \"ProductVersion\", \"${LUMORA_DISPLAY_VERSION}\"
      VALUE \"OriginalFilename\", \"lumora.exe\"
    END
  END
  BLOCK \"VarFileInfo\"
  BEGIN
    VALUE \"Translation\", 0x409, 1200
  END
END
")
    target_sources(lumora PRIVATE ${CMAKE_BINARY_DIR}/lumora.rc)
  endif()
  target_include_directories(lumora PRIVATE src/ui)
  target_link_libraries(lumora PRIVATE lumora_core Qt6::Widgets Qt6::Svg)
  if(WIN32)
    target_link_libraries(lumora PRIVATE crypt32)   # DPAPI, for "Stay signed in"
  endif()
  add_custom_command(TARGET lumora POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/fixtures $<TARGET_FILE_DIR:lumora>/fixtures
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/docs/Lumora-Manual.pdf $<TARGET_FILE_DIR:lumora>/Lumora-Manual.pdf)

  # Rebuilds the user guide PDF from docs/manual/manual.md (needs Python 3 and reportlab):
  #   cmake --build build --target manual
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    add_custom_target(manual
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/docs/manual/build_manual.py
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/docs/Lumora-Manual.pdf $<TARGET_FILE_DIR:lumora>/Lumora-Manual.pdf
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Building docs/Lumora-Manual.pdf"
      VERBATIM)
  endif()

  # The installer (cmake/Packaging.cmake, make-msi.bat) comes with the release zips; the GitHub
  # repository leaves it out, and builds without it.
  if(EXISTS ${CMAKE_SOURCE_DIR}/cmake/Packaging.cmake)
    include(cmake/Packaging.cmake)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(LUMORA_BUILD_TESTS)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  enable_testing()
  add_executable(lumora_tests tests/test_core.cpp)
  target_link_libraries(lumora_tests PRIVATE lumora_core Qt6::Test)
  add_test(NAME lumora_tests COMMAND lumora_tests)
endif()
