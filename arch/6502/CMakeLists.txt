# Build configuration
set(CMAKE_BUILD_TYPE Debug)

# GCC/Clang debug flags — MSVC has its own defaults via the VS generator
if(NOT MSVC)
    set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
endif()


add_library(lib6502 6502.c 2c02.c nesbus.c 2a03.c cartridge.c mapper.c controller.c
			nes_input.c mapper_000.c mapper_001.c mapper_002.c mapper_003.c mapper_004.c mapper_005.c mapper_007.c mapper_009.c mapper_011.c mapper_019.c mapper_066.c mapper_069.c mapper_071.c
			savestate.c sram.c debug.c disasm.c rewind.c tas.c gamepad.c)

if(MSVC)
    # On Linux, SDL2 includes are added globally via include_directories().
    # On MSVC they only propagate through imported targets, so declare the
    # dependency explicitly here (nes_input.c includes SDL2/SDL.h).
    target_link_libraries(lib6502 PRIVATE SDL2::SDL2-static)
    # Suppress MSVC deprecation warnings for standard C functions (fopen etc.)
    target_compile_definitions(lib6502 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
