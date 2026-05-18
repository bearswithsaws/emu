add_executable(test_ppu_clock test_ppu_clock.c)

target_link_libraries(test_ppu_clock PRIVATE lib6502)

target_include_directories(test_ppu_clock PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_apu test_apu.c)
target_link_libraries(test_apu PRIVATE lib6502 m)
target_include_directories(test_apu PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_001 test_mapper_001.c)
target_link_libraries(test_mapper_001 PRIVATE lib6502)
target_include_directories(test_mapper_001 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_002 test_mapper_002.c)
target_link_libraries(test_mapper_002 PRIVATE lib6502)
target_include_directories(test_mapper_002 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_003 test_mapper_003.c)
target_link_libraries(test_mapper_003 PRIVATE lib6502)
target_include_directories(test_mapper_003 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_004 test_mapper_004.c)
target_link_libraries(test_mapper_004 PRIVATE lib6502)
target_include_directories(test_mapper_004 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_011 test_mapper_011.c)
target_link_libraries(test_mapper_011 PRIVATE lib6502)
target_include_directories(test_mapper_011 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_066 test_mapper_066.c)
target_link_libraries(test_mapper_066 PRIVATE lib6502)
target_include_directories(test_mapper_066 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_007 test_mapper_007.c)

add_executable(test_mapper_009 test_mapper_009.c)
target_link_libraries(test_mapper_007 PRIVATE lib6502)
target_include_directories(test_mapper_007 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

target_link_libraries(test_mapper_009 PRIVATE lib6502)
target_include_directories(test_mapper_009 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_cpu_nestest test_cpu_nestest.c)
target_link_libraries(test_cpu_nestest PRIVATE lib6502)
target_include_directories(test_cpu_nestest PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)
target_compile_definitions(test_cpu_nestest PRIVATE
    NESTEST_ROM_PATH="${CMAKE_SOURCE_DIR}/tests/roms/nestest.nes"
)

add_executable(test_disasm test_disasm.c)
target_link_libraries(test_disasm PRIVATE lib6502)
target_include_directories(test_disasm PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

enable_testing()
add_test(NAME ppu_clock    COMMAND test_ppu_clock)
add_test(NAME apu          COMMAND test_apu)
add_test(NAME mapper_001   COMMAND test_mapper_001)
add_test(NAME mapper_002   COMMAND test_mapper_002)
add_test(NAME mapper_003   COMMAND test_mapper_003)
add_test(NAME mapper_004   COMMAND test_mapper_004)
add_test(NAME mapper_007   COMMAND test_mapper_007)
add_test(NAME mapper_009   COMMAND test_mapper_009)
add_test(NAME mapper_011   COMMAND test_mapper_011)
add_test(NAME mapper_066   COMMAND test_mapper_066)
add_test(NAME cpu_nestest  COMMAND test_cpu_nestest)
add_test(NAME disasm       COMMAND test_disasm)
