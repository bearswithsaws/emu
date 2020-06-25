cmake_minimum_required(VERSION 3.10)

# set the project name
project(emu VERSION 0.1)


INCLUDE(FindPkgConfig)
PKG_SEARCH_MODULE(SDL2 REQUIRED sdl2)
PKG_SEARCH_MODULE(SDL2TTF REQUIRED SDL2_ttf>=2.0.0)
include_directories(${SDL2_INCLUDE_DIRS} ${SDL2TTF_INCLUDE_DIRS})

configure_file(emu_config.h.in emu_config.h)

# Add the architecures
add_subdirectory(arch)

set(CMAKE_BUILD_TYPE Debug)

# add the executable
add_executable(emu emu.c gui.c)

target_link_libraries(emu PUBLIC lib6502 ${SDL2_LIBRARIES} ${SDL2TTF_LIBRARIES})

target_include_directories(emu PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           "${PROJECT_SOURCE_DIR}/arch/6502"
                           )