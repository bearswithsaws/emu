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

# Windows-specific: suppress console window and static-link GCC/C++ runtimes.
# libwinpthread cannot be statically linked because SDL2.dll itself carries a
# runtime dependency on it — Windows would still require the DLL when loading
# SDL2. We therefore bundle all required MinGW DLLs next to emu.exe instead.
# Issue #64 (Windows packaging) will replace this with an ntldd-based full
# dependency walk in the GitHub Actions release workflow.
if(MINGW)
    target_link_options(emu PRIVATE -mwindows -static-libgcc -static-libstdc++)

    # DLLs that must ship alongside emu.exe.  SDL2 is the main dependency;
    # libwinpthread is a transitive dependency of SDL2.dll itself.
    set(_MINGW_DLLS SDL2.dll libwinpthread-1.dll)

    foreach(_DLL ${_MINGW_DLLS})
        find_file(_DLL_PATH "${_DLL}"
            HINTS "${SDL2_LIBDIR}/../bin"
            ENV PATH
        )
        if(_DLL_PATH)
            add_custom_command(TARGET emu POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_DLL_PATH}" "$<TARGET_FILE_DIR:emu>"
                COMMENT "Copying ${_DLL} to output directory"
            )
        else()
            message(WARNING "${_DLL} not found — copy it next to emu.exe manually")
        endif()
        unset(_DLL_PATH CACHE)
    endforeach()
endif()