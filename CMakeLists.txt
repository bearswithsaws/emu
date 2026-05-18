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

# Windows-specific: suppress console window and static-link MinGW runtime so
# emu.exe runs from Explorer without a terminal or extra DLLs.
if(MINGW)
    # -mwindows: GUI subsystem, no console window.
    # -static-libgcc/-static-libstdc++: bake GCC/C++ runtimes into the exe.
    target_link_options(emu PRIVATE -mwindows -static-libgcc -static-libstdc++)

    # Static-link libwinpthread. The -Bstatic/-Bdynamic pair is placed via
    # target_link_libraries so it lands in the library section of the link
    # command (after SDL2), not before it — which would break SDL2.
    target_link_libraries(emu PRIVATE -Wl,-Bstatic -lpthread -Wl,-Bdynamic)

    # Copy SDL2.dll next to emu.exe at build time so it's found when the exe
    # is launched from Explorer.
    find_file(SDL2_DLL_PATH SDL2.dll
        HINTS "${SDL2_LIBDIR}/../bin"
        ENV PATH
    )
    if(SDL2_DLL_PATH)
        add_custom_command(TARGET emu POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_DLL_PATH}" "$<TARGET_FILE_DIR:emu>"
            COMMENT "Copying SDL2.dll to output directory"
        )
    else()
        message(WARNING "SDL2.dll not found — copy it next to emu.exe manually")
    endif()
endif()