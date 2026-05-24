cmake_minimum_required(VERSION 3.10)

# set the project name
project(emu VERSION 1.0.1 LANGUAGES C CXX)

# Require C11 globally — needed for stdatomic.h (_Atomic) on all platforms
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Ensure ImGui submodule is present
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/lib/ui/vendor/imgui/imgui.h")
    message(FATAL_ERROR
        "ImGui submodule not found. Run:\n"
        "  git submodule update --init --recursive")
endif()

# SDL2: vcpkg imported targets on MSVC, pkg-config on Linux/MinGW
if(MSVC)
    find_package(SDL2 CONFIG REQUIRED)
    # SDL2::SDL2-static carries its own include dirs; SDL2::SDL2main provides
    # the WinMain shim required for WIN32 subsystem executables.
    set(SDL2_LIBRARIES SDL2::SDL2-static SDL2::SDL2main)
else()
    include(FindPkgConfig)
    pkg_search_module(SDL2 REQUIRED sdl2)
    include_directories(${SDL2_INCLUDE_DIRS})
endif()

configure_file(emu_config.h.in emu_config.h)

option(NES_DEBUG "Enable debug logging" OFF)
if(NES_DEBUG)
    add_compile_definitions(NES_DEBUG)
endif()

add_definitions(-DINVALID_AS_NOP)

# Shared platform libraries
add_subdirectory(lib)

# CPU cores
add_subdirectory(cpu)

# Console machines
add_subdirectory(consoles)

# Tests
enable_testing()
add_subdirectory(tests)

# Build configuration
set(CMAKE_BUILD_TYPE Debug)

# GCC/Clang debug flags — MSVC has its own defaults via the VS generator
if(NOT MSVC)
    set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
endif()

# add the executable
# On Windows, include the .rc file so CMake compiles it with rc.exe (MSVC)
# or windres (MinGW) and links the icon resource into the binary.
if(WIN32)
    add_executable(emu emu.c emu.rc)
else()
    add_executable(emu emu.c)
endif()

target_link_libraries(emu PUBLIC lib_nes libdisplay libinput libui ${SDL2_LIBRARIES})

target_include_directories(emu PUBLIC
    "${PROJECT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/consoles/nes"
    "${PROJECT_SOURCE_DIR}/consoles/nes/mappers"
    "${PROJECT_SOURCE_DIR}/cpu/6502"
    "${PROJECT_SOURCE_DIR}/lib/input"
    "${PROJECT_SOURCE_DIR}/lib/display"
    "${PROJECT_SOURCE_DIR}/lib/ui"
)

# Install targets (used by AppImage and Linux packaging)
install(TARGETS emu DESTINATION bin)
install(FILES assets/emu.desktop DESTINATION share/applications)
install(FILES assets/icon.png    DESTINATION share/pixmaps RENAME emu.png)
install(FILES assets/icon.png    DESTINATION share/icons/hicolor/256x256/apps RENAME emu.png)

# Windows: suppress console window.
# MSVC: SDL2 is statically linked via vcpkg x64-windows-static — no extra DLLs needed.
# MinGW: static-link GCC/C++ runtimes; SDL2.dll + libwinpthread-1.dll must be
#         copied next to the exe for local dev builds (release packaging is done
#         by the GitHub Actions workflow).
if(MSVC)
    set_target_properties(emu PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(MINGW)
    target_link_options(emu PRIVATE -mwindows -static-libgcc -static-libstdc++)

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
