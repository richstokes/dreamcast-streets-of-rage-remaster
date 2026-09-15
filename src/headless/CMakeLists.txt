cmake_minimum_required(VERSION 3.24)
project(SoRHeadless LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${ROOT}/build/native/upstream/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "arithmetic-probes.cpp$")
add_executable(sor-headless ${GAME_SOURCES} main.cpp
    ../dreamcast/runtime.cpp ../dreamcast/game_bindings.cpp ../dreamcast/replay.cpp
    ../core/memory.c)
target_include_directories(sor-headless PRIVATE ../dreamcast ../dreamcast/compat
    "${ROOT}/include" "${ROOT}/build/native/upstream")
