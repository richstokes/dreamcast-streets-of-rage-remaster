cmake_minimum_required(VERSION 3.24)
project(SoRHeadless LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${ROOT}/build/native/upstream/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "arithmetic-probes.cpp$")
add_executable(sor-headless ${GAME_SOURCES} main.cpp extract_frames.cpp
    ../dreamcast/runtime.cpp ../dreamcast/game_bindings.cpp ../dreamcast/replay.cpp
    ../dreamcast/cheats.cpp ../dreamcast/cheat_menu.cpp
    ../core/memory.c ../render/vdp_scene.cpp ../render/sprite_probe.cpp ../render/art_catalog.cpp ../render/scene_light.cpp ../render/scene_particles.cpp ../render/scene_weather.cpp ../audio/audio_core.cpp ../audio/dac_driver.cpp)
target_compile_definitions(sor-headless PRIVATE SOR_PC_HISTOGRAM)
target_include_directories(sor-headless PRIVATE ../dreamcast ../dreamcast/compat
    "${ROOT}/include" "${ROOT}/build/native/upstream" ../render ../audio)
find_package(ZLIB REQUIRED)
target_link_libraries(sor-headless PRIVATE ZLIB::ZLIB)
