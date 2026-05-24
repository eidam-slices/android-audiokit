cmake_minimum_required(VERSION 3.22)
project("core_native" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(AUBIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/aubio/src")
if(NOT EXISTS "${AUBIO_ROOT}")
    message(FATAL_ERROR "aubio sources not found at ${AUBIO_ROOT}")
endif()

# Inteligentní generování config.h pro všechny platformy
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config.h" "
#ifndef CMAKE_CONFIG_H
#define CMAKE_CONFIG_H

#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_MATH_H 1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDARG_H 1
#define HAVE_C99_VARARGS_MACROS 1

#ifndef _MSC_VER
#define HAVE_UNISTD_H 1
#define HAVE_GETOPT_H 1
#endif

#endif
")

file(GLOB_RECURSE AUBIO_SOURCES CONFIGURE_DEPENDS "${AUBIO_ROOT}" "*.c")
list(FILTER AUBIO_SOURCES EXCLUDE REGEX "python/ext|tests/|examples/")
add_library(aubio STATIC ${AUBIO_SOURCES})
set_target_properties(aubio PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(aubio PUBLIC "${AUBIO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(aubio PRIVATE HAVE_CONFIG_H)
target_link_libraries(aubio PUBLIC m)

# Správné větvení flagů podle kompilátoru
if(MSVC)
    target_compile_options(aubio PRIVATE /Zc:preprocessor)
else()
    target_compile_options(aubio PRIVATE -std=gnu99 -Wno-implicit-function-declaration)
endif()

file(GLOB_RECURSE CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER CORE_SOURCES INCLUDE REGEX "core_audio_engine\\.cpp$")
add_library(core_native STATIC ${CORE_SOURCES})
set_target_properties(core_native PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(core_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(core_native PRIVATE "${AUBIO_ROOT}")

if (DEFINED JNI_INCLUDE_DIRS AND NOT "${JNI_INCLUDE_DIRS}" STREQUAL "")
    target_include_directories(core_native PRIVATE ${JNI_INCLUDE_DIRS})
endif()

find_package(Threads REQUIRED)
target_link_libraries(core_native PUBLIC aubio m Threads::Threads)

message(STATUS "Configured core_native (aubio + engine)")
