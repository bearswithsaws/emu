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

enable_testing()
add_test(NAME ppu_clock COMMAND test_ppu_clock)
add_test(NAME apu COMMAND test_apu)
add_test(NAME mapper_001 COMMAND test_mapper_001)
add_test(NAME mapper_002 COMMAND test_mapper_002)
add_test(NAME mapper_003 COMMAND test_mapper_003)
add_test(NAME mapper_004 COMMAND test_mapper_004)
