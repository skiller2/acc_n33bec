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

execute_process(
  COMMAND git rev-list --count HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_BUILD
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if(NOT GIT_SHA)
  set(GIT_SHA "unknown")
endif()

if(NOT GIT_TAG)
  set(GIT_TAG "unknown")
endif()

set(PROJECT_VERSION "${GIT_TAG}+${GIT_BUILD}-${GIT_SHA}")



add_compile_definitions(PROJECT_VERSION="${PROJECT_VERSION}") 

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(acc_n33bec)



# Directorio web
set(WEB_DIR ${CMAKE_CURRENT_LIST_DIR}/web)

# Bundle generado
set(WEB_BUNDLE ${CMAKE_BINARY_DIR}/web_bundle.bin)

set(OTA_FW ${CMAKE_BINARY_DIR}/acc_n33bec.bin)

# Bundle generado
set(FULL_FW "${CMAKE_BINARY_DIR}/acc-n33bec-fw-${GIT_TAG}.bin")

# Buscar todos los archivos web
file(GLOB_RECURSE WEB_FILES
    "${WEB_DIR}/*"
)


add_custom_command(
    OUTPUT ${WEB_BUNDLE}
    COMMAND ${python}
            ${CMAKE_CURRENT_LIST_DIR}/tools/make_web_bundle.py
            ${WEB_BUNDLE}
            ${WEB_FILES}
    DEPENDS
            ${WEB_FILES}
            ${python} ${CMAKE_CURRENT_LIST_DIR}/tools/make_web_bundle.py
    COMMENT "Generating web bundle"
)


add_custom_command(
    OUTPUT ${FULL_FW}
    COMMAND ${python}
            ${CMAKE_CURRENT_LIST_DIR}/tools/make_update_bundle.py
            ${FULL_FW}
            ${OTA_FW}
            ${WEB_BUNDLE}
    DEPENDS
    app
    ${WEB_BUNDLE}
    COMMENT "Generating firmare integrated"
)

add_custom_target(generate_web_bundle ALL
    DEPENDS ${WEB_BUNDLE}
)

add_custom_target(generate_full_firmware ALL
    DEPENDS ${FULL_FW}
)
