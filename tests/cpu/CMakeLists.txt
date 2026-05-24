add_executable(test_cpu_nestest test_cpu_nestest.c)
target_link_libraries(test_cpu_nestest PRIVATE lib_cpu_6502)
target_include_directories(test_cpu_nestest PRIVATE
    "${PROJECT_SOURCE_DIR}/cpu/6502"
    "${PROJECT_BINARY_DIR}"
)
target_compile_definitions(test_cpu_nestest PRIVATE
    NESTEST_ROM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/roms/nestest.nes"
)

add_executable(test_disasm test_disasm.c)
target_link_libraries(test_disasm PRIVATE lib_cpu_6502)
target_include_directories(test_disasm PRIVATE
    "${PROJECT_SOURCE_DIR}/cpu/6502"
    "${PROJECT_BINARY_DIR}"
)

add_test(NAME cpu_nestest COMMAND test_cpu_nestest)
add_test(NAME disasm      COMMAND test_disasm)
