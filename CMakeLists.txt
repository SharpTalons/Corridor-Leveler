cmake_minimum_required(VERSION 3.22)
project(CorridorLeveler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Static MSVC runtime: the plugins and OBS filter need no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CORRIDOR_BUILD_TESTS  "Build the DSP core tests" ON)
option(CORRIDOR_BUILD_PLUGIN "Build the VST3 / VST2 / Standalone plugin (JUCE)" ON)
option(CORRIDOR_BUILD_OBS    "Build the native OBS filter" ON)

# ---- shared DSP core (no framework) ----
add_library(corridor_core STATIC core/src/Processor.cpp)
target_include_directories(corridor_core PUBLIC core/include)
if(MSVC)
  target_compile_options(corridor_core PRIVATE /W4 /fp:fast)
else()
  target_compile_options(corridor_core PRIVATE -Wall -Wextra -O2)
endif()

if(CORRIDOR_BUILD_TESTS)
  enable_testing()
  add_executable(corridor_tests tests/test_core.cpp)
  target_link_libraries(corridor_tests PRIVATE corridor_core)
  if(MSVC)
    target_compile_definitions(corridor_tests PRIVATE _USE_MATH_DEFINES)
  endif()
  add_test(NAME core COMMAND corridor_tests)
endif()

if(CORRIDOR_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()

if(CORRIDOR_BUILD_OBS)
  add_subdirectory(obs)
endif()
