cmake_minimum_required(VERSION 3.22)
project("audiokit" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

## Ukládat kompilované objekty do samostatné složky
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/libs")
#set(CORE_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../core-lib/src/main/cpp")

# !!! ZMĚNA: CMake teď sype výsledky rovnou do složky, kterou Gradle balí do JARu !!!
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/clibs/${PLATFORM_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/clibs/${PLATFORM_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/clibs/${PLATFORM_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/clibs/${PLATFORM_DIR}")

set(CORE_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../core-lib/src/main/cpp")

# Najdeme JNI a Thready
find_package(JNI REQUIRED)
find_package(Threads REQUIRED)

# 1. Přidáme core_native (aubio + core)
add_subdirectory("${CORE_NATIVE_DIR}" core_build)

## Build RtAudio statically so it gets baked into libaudiokit.dylib
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "Build only static RtAudio lib" FORCE)
set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "Build static RtAudio lib" FORCE)

# 2. Add the local RtAudio submodule
add_subdirectory(rtaudio)

# 3. Sestavíme výslednou audiokit knihovnu
add_library(audiokit SHARED desktop_audio_source.cpp)

# Compile the JNI glue directly into the desktop shared library so the
# Java_* symbols are present at runtime (avoids static-linker discarding
# unreferenced objects from the core_native archive).
target_sources(audiokit PRIVATE "${CORE_NATIVE_DIR}/jni_glue.cpp")

# Nastavíme include cesty pro tvůj kód (včetně lokálního rtaudio)
target_include_directories(audiokit PRIVATE
        "${CORE_NATIVE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/rtaudio"  # Cesta k hlavičkám tvého submodulu
        ${JNI_INCLUDE_DIRS}
)

# Slinkujeme to všechno dohromady
target_link_libraries(audiokit PRIVATE
        core_native
        rtaudio
        Threads::Threads
)

if(NOT MSVC)
        target_link_libraries(audiokit PRIVATE m)
endif()

message(STATUS "Configured audiokit using local RtAudio submodule")
