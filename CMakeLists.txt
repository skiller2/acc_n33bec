# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

execute_process(
  COMMAND git describe --tags --always --dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if(NOT GIT_SHA)
  set(GIT_SHA "unknown")
endif()

if(NOT GIT_TAG)
  set(GIT_TAG "unknown")
endif()

set(PROJECT_VERSION "${GIT_TAG}-${GIT_SHA}")
add_compile_definitions(PROJECT_VERSION=\"${PROJECT_VERSION}\")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(acc_n33bec)
