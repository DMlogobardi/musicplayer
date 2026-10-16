cmake_minimum_required(VERSION 3.14)
project(AudioEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Scarica e integra Oboe automaticamente da GitHub durante la compilazione
include(FetchContent)
FetchContent_Declare(
    oboe
    GIT_REPOSITORY https://github.com/google/oboe.git
    GIT_TAG        v1.9.0 # o la versione desiderata
)
FetchContent_MakeAvailable(oboe)

# La tua libreria C++ (crea il file .so per Android)
add_library(audio_engine SHARED
    OboeAudioDriver.cpp
    # Aggiungi qui gli altri file .cpp del tuo core audio (es. AudioDeviceManager.cpp)
)

target_include_directories(audio_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Collega Oboe e le librerie di sistema Android (log, android)
target_link_libraries(audio_engine PRIVATE
    oboe
    android
    log
)