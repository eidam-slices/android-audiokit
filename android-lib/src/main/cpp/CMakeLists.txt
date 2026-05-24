cmake_minimum_required(VERSION 3.22.1)
project("audiokit" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CORE_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../core-lib/src/main/cpp")

# Add core_native as subdirectory (builds aubio + core_native static libs)
add_subdirectory("${CORE_NATIVE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/core_build")

find_package(oboe REQUIRED CONFIG)

# Build Android-specific library linking core_native + Oboe
add_library(audiokit SHARED android_audio_source.cpp)

# Compile JNI glue directly into the Android shared library as well so the
# Java_* symbols are available to the JVM. On Android we previously used
# --whole-archive to force-link core_native; compiling the glue here keeps
# behavior consistent across platforms.
target_sources(audiokit PRIVATE "${CORE_NATIVE_DIR}/jni_glue.cpp")

target_include_directories(audiokit PRIVATE
    "${CORE_NATIVE_DIR}/include"
    "${CORE_NATIVE_DIR}"
)

target_link_libraries(audiokit
    PRIVATE
    core_native
    oboe::oboe
    m
    log
)