add_executable(test_ppu_clock test_ppu_clock.c)

target_link_libraries(test_ppu_clock PRIVATE lib6502)

target_include_directories(test_ppu_clock PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

enable_testing()
add_test(NAME ppu_clock COMMAND test_ppu_clock)
