cmake_minimum_required(VERSION 3.10)

# set the project name
project(emu VERSION 0.1)


INCLUDE(FindPkgConfig)
PKG_SEARCH_MODULE(SDL2 REQUIRED sdl2)
PKG_SEARCH_MODULE(SDL2TTF REQUIRED SDL2_ttf>=2.0.0)
include_directories(${SDL2_INCLUDE_DIRS} ${SDL2TTF_INCLUDE_DIRS})

configure_file(emu_config.h.in emu_config.h)

# add_definitions( -DDEBUG )
add_definitions( -DINVALID_AS_NOP )

# Add shared libraries
add_subdirectory(lib)

# Add the architecures
add_subdirectory(arch)

# Build configuration
set(CMAKE_BUILD_TYPE Debug)

# Enable full debug symbols and disable optimizations for easier debugging
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")


# add the executable
add_executable(emu emu.c)

target_link_libraries(emu PUBLIC lib6502 libdisplay ${SDL2_LIBRARIES} ${SDL2TTF_LIBRARIES})

target_include_directories(emu PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           "${PROJECT_SOURCE_DIR}/arch/6502"
                           "${PROJECT_SOURCE_DIR}/lib/display"
                           )