cmake_minimum_required(VERSION 3.22)
# Version: MAJOR.MINOR from the VERSION file, PATCH supplied by CI (number of commits on main).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CORRIDOR_BASE_VERSION LIMIT_COUNT 1)
string(STRIP "${CORRIDOR_BASE_VERSION}" CORRIDOR_BASE_VERSION)
set(CORRIDOR_VERSION "${CORRIDOR_BASE_VERSION}.0" CACHE STRING "Version baked into the plugins and installer")
project(CorridorLeveler VERSION ${CORRIDOR_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Static MSVC runtime: the plugins need no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CORRIDOR_BUILD_TESTS  "Build the DSP core tests" ON)
option(CORRIDOR_BUILD_PLUGIN "Build the VST3 and VST2 plugins (JUCE)" ON)

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
