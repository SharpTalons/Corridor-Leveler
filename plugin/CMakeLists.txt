# VST3 and VST 2.x builds, made with JUCE (AGPLv3 open-source licence).
# The 2.x format is built against FST (GPLv3), an open reimplementation of the 2.x headers,
# because Steinberg no longer distributes that SDK.
include(FetchContent)

set(CORRIDOR_JUCE_DIR "" CACHE PATH "Use an existing JUCE checkout instead of downloading")
set(CORRIDOR_FST_DIR "" CACHE PATH "Use an existing FST checkout instead of downloading")
option(CORRIDOR_BUILD_VST2 "Build the 2.x plugin (what OBS loads through its built-in VST 2.x filter)" ON)

if(CORRIDOR_JUCE_DIR)
  add_subdirectory("${CORRIDOR_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

set(_formats VST3)
if(CORRIDOR_BUILD_VST2)
  if(NOT CORRIDOR_FST_DIR)
    # download only: SOURCE_SUBDIR has no CMakeLists.txt, so FST's own project is not added
    FetchContent_Declare(fst GIT_REPOSITORY https://git.iem.at/zmoelnig/FST.git GIT_TAG 647af068765b75867e3a28b4dd8991ab9ed47f7c
      SOURCE_SUBDIR download-only)
    FetchContent_MakeAvailable(fst)
    set(CORRIDOR_FST_DIR "${fst_SOURCE_DIR}")
  endif()
  # JUCE looks for <pluginterfaces/vst2.x/aeffect.h>; point those names at FST.
  set(_sdk "${CMAKE_CURRENT_BINARY_DIR}/vst2-fst")
  file(MAKE_DIRECTORY "${_sdk}/pluginterfaces/vst2.x")
  file(WRITE "${_sdk}/pluginterfaces/vst2.x/aeffect.h"  "#pragma once\n#include \"${CORRIDOR_FST_DIR}/fst/fst.h\"\n")
  file(WRITE "${_sdk}/pluginterfaces/vst2.x/aeffectx.h" "#pragma once\n#include \"${CORRIDOR_FST_DIR}/fst/fst.h\"\n")
  juce_set_vst2_sdk_path("${_sdk}")
  list(APPEND _formats VST)
endif()

juce_add_plugin(CorridorLeveler
  PRODUCT_NAME "Corridor Leveler"
  COMPANY_NAME "Corridor"
  BUNDLE_ID "audio.corridor.leveler"
  PLUGIN_MANUFACTURER_CODE Crdr
  PLUGIN_CODE Crlv
  FORMATS ${_formats}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Fx Dynamics
  VST2_CATEGORY kPlugCategEffect
  COPY_PLUGIN_AFTER_BUILD FALSE)

# the interface typeface: IBM Plex Sans (SIL OFL 1.1, see fonts/OFL.txt), unmodified. Its figures are tabular,
# so live numbers don't shift as they change, and its zero is plain, so it never reads as an 8 at small sizes.
juce_add_binary_data(corridor_fonts NAMESPACE CorridorFonts SOURCES fonts/IBMPlexSans-Regular.ttf fonts/IBMPlexSans-Bold.ttf)

target_sources(CorridorLeveler PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp src/Updates.cpp)
target_compile_definitions(CorridorLeveler PRIVATE CORRIDOR_VERSION_STRING="${PROJECT_VERSION}")
target_compile_definitions(CorridorLeveler PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)
target_link_libraries(CorridorLeveler
  PRIVATE corridor_core corridor_fonts juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Loads the built binaries like a host does and renders the window to PNG (see tests/plugin_check.cpp)
option(CORRIDOR_BUILD_PLUGIN_CHECK "Build the plugin host check tool" ON)
if(CORRIDOR_BUILD_PLUGIN_CHECK)
  juce_add_console_app(corridor_plugin_check PRODUCT_NAME "corridor_plugin_check")
  target_sources(corridor_plugin_check PRIVATE ../tests/plugin_check.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Updates.cpp)
  target_compile_definitions(corridor_plugin_check PRIVATE CORRIDOR_VERSION_STRING="${PROJECT_VERSION}")
  target_compile_definitions(corridor_plugin_check PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(corridor_plugin_check PRIVATE corridor_core corridor_fonts juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics
    juce::juce_recommended_config_flags ${CMAKE_DL_LIBS})
  # the 2.x host lives in its own file and sees only FST, never JUCE's 2.x hosting code
  add_library(corridor_vst2_host STATIC ../tests/vst2_host.cpp)
  if(CORRIDOR_BUILD_VST2)
    target_include_directories(corridor_vst2_host PRIVATE "${CORRIDOR_FST_DIR}/fst")
  endif()
  target_compile_definitions(corridor_vst2_host PRIVATE _USE_MATH_DEFINES)
  target_link_libraries(corridor_plugin_check PRIVATE corridor_vst2_host)
  add_dependencies(corridor_plugin_check CorridorLeveler_VST3)
endif()
