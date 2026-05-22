cmake_minimum_required(VERSION 3.22.1)
project("audiokit" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(oboe REQUIRED CONFIG)

set(AUBIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/aubio/src")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config.h" "#ifndef CMAKE_CONFIG_H\n#define CMAKE_CONFIG_H\n#define HAVE_STDLIB_H 1\n#define HAVE_STDIO_H 1\n#define HAVE_MATH_H 1\n#define HAVE_STRING_H 1\n#define HAVE_ERRNO_H 1\n#define HAVE_LIMITS_H 1\n#define HAVE_STDARG_H 1\n#define HAVE_UNISTD_H 1\n#define HAVE_GETOPT_H 1\n#endif\n")

file(GLOB_RECURSE AUBIO_SOURCES CONFIGURE_DEPENDS "${AUBIO_ROOT}/*.c")

add_library(aubio STATIC ${AUBIO_SOURCES})
set_target_properties(aubio PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(aubio PUBLIC "${AUBIO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(aubio PRIVATE HAVE_CONFIG_H)

add_library(audiokit SHARED native-lib.cpp)
target_include_directories(audiokit PRIVATE "${AUBIO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}")


target_link_libraries(
        audiokit
        aubio
        oboe::oboe
        m
        log
)