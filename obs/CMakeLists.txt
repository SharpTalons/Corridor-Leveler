# Native OBS filter. Only libobs *headers* are needed: on Windows the import library is
# generated from obs.def (the libobs functions this filter calls), so libobs itself is not built.
include(FetchContent)

set(CORRIDOR_OBS_VERSION "30.0.0" CACHE STRING "OBS Studio version whose libobs headers to build against")
set(CORRIDOR_LIBOBS_INCLUDE_DIR "" CACHE PATH "Use an existing libobs source folder instead of downloading")

if(NOT CORRIDOR_LIBOBS_INCLUDE_DIR)
  FetchContent_Declare(obs_src
    URL "https://github.com/obsproject/obs-studio/archive/refs/tags/${CORRIDOR_OBS_VERSION}.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_GetProperties(obs_src)
  if(NOT obs_src_POPULATED)
    FetchContent_Populate(obs_src)
  endif()
  set(CORRIDOR_LIBOBS_INCLUDE_DIR "${obs_src_SOURCE_DIR}/libobs")
endif()

add_library(corridor-leveler MODULE corridor-obs.cpp)
target_include_directories(corridor-leveler PRIVATE "${CORRIDOR_LIBOBS_INCLUDE_DIR}")
target_link_libraries(corridor-leveler PRIVATE corridor_core)
set_target_properties(corridor-leveler PROPERTIES PREFIX "")

if(WIN32)
  set(_obs_lib "${CMAKE_CURRENT_BINARY_DIR}/obs.lib")
  add_custom_command(OUTPUT "${_obs_lib}"
    COMMAND "${CMAKE_AR}" /nologo /def:"${CMAKE_CURRENT_SOURCE_DIR}/obs.def" /out:"${_obs_lib}" /machine:x64
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/obs.def"
    COMMENT "Generating obs.lib import library")
  add_custom_target(corridor_obs_importlib DEPENDS "${_obs_lib}")
  add_dependencies(corridor-leveler corridor_obs_importlib)
  target_link_libraries(corridor-leveler PRIVATE "${_obs_lib}")
  target_compile_definitions(corridor-leveler PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  if(MSVC)
    target_compile_options(corridor-leveler PRIVATE /wd4996 /wd4244)
  endif()
elseif(UNIX AND NOT APPLE)
  # libobs symbols are resolved by OBS at load time
  target_link_options(corridor-leveler PRIVATE "-Wl,--allow-shlib-undefined")
endif()

# End-to-end check of the filter with a stand-in for libobs (Linux only)
if(CORRIDOR_BUILD_TESTS AND UNIX AND NOT APPLE)
  add_executable(obs_harness ../tests/obs_harness.cpp)
  target_include_directories(obs_harness PRIVATE "${CORRIDOR_LIBOBS_INCLUDE_DIR}")
  set_target_properties(obs_harness PROPERTIES ENABLE_EXPORTS ON)
  target_link_libraries(obs_harness PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(obs_harness corridor-leveler)
  add_test(NAME obs_filter COMMAND obs_harness $<TARGET_FILE:corridor-leveler>)
endif()
