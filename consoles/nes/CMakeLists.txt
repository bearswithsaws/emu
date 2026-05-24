set(CMAKE_BUILD_TYPE Debug)
if(NOT MSVC)
    set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
endif()

add_library(lib_nes
    ppu.c
    apu.c
    bus.c
    cartridge.c
    controller.c
    input.c
    debug.c
    savestate.c
    rewind.c
    sram.c
    tas.c
    mappers/mapper.c
    mappers/mapper_000.c
    mappers/mapper_001.c
    mappers/mapper_002.c
    mappers/mapper_003.c
    mappers/mapper_004.c
    mappers/mapper_005.c
    mappers/mapper_007.c
    mappers/mapper_009.c
    mappers/mapper_011.c
    mappers/mapper_019.c
    mappers/mapper_066.c
    mappers/mapper_069.c
    mappers/mapper_071.c
)

target_include_directories(lib_nes PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mappers
    ${PROJECT_SOURCE_DIR}/cpu/6502
    ${PROJECT_SOURCE_DIR}/lib/input
)

target_link_libraries(lib_nes PUBLIC lib_cpu_6502)

# lib_nes is SDL2-free.  All SDL2 usage lives in the host layer (emu.c,
# libdisplay, libinput).  The only platform-specific dependency allowed
# here is the C standard library.
if(MSVC)
    # Suppress MSVC deprecation warnings for fopen/sprintf/etc.
    target_compile_definitions(lib_nes PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
