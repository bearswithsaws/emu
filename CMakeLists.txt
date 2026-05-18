cmake_minimum_required(VERSION 3.10)

# set the project name
project(emu VERSION 0.1 LANGUAGES C CXX)

# Ensure ImGui submodule is present
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/lib/ui/vendor/imgui/imgui.h")
    message(FATAL_ERROR
        "ImGui submodule not found. Run:\n"
        "  git submodule update --init --recursive")
endif()

INCLUDE(FindPkgConfig)
PKG_SEARCH_MODULE(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})

configure_file(emu_config.h.in emu_config.h)

option(NES_DEBUG "Enable debug logging" OFF)
if(NES_DEBUG)
    add_compile_definitions(NES_DEBUG)
endif()

add_definitions( -DINVALID_AS_NOP )

# Add shared libraries
add_subdirectory(lib)

# Add the architecures
add_subdirectory(arch)

# Tests
enable_testing()
add_subdirectory(tests)

# Build configuration
set(CMAKE_BUILD_TYPE Debug)

# Enable full debug symbols and disable optimizations for easier debugging
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")


# add the executable
add_executable(emu emu.c)

target_link_libraries(emu PUBLIC lib6502 libdisplay libui ${SDL2_LIBRARIES})

target_include_directories(emu PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           "${PROJECT_SOURCE_DIR}/arch/6502"
                           "${PROJECT_SOURCE_DIR}/lib/display"
                           "${PROJECT_SOURCE_DIR}/lib/ui"
                           )