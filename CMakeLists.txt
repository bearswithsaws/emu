cmake_minimum_required(VERSION 3.10)

# set the project name
project(emu VERSION 0.1)

configure_file(emu_config.h.in emu_config.h)

# Add the architecures
add_subdirectory(arch)

# add the executable
add_executable(emu emu.c)

target_link_libraries(emu PUBLIC lib6502)

target_include_directories(emu PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           "${PROJECT_SOURCE_DIR}/arch/6502"
                           )