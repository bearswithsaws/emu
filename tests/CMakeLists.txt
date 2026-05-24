add_executable(test_ppu_clock test_ppu_clock.c)

target_link_libraries(test_ppu_clock PRIVATE lib6502)

target_include_directories(test_ppu_clock PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_apu test_apu.c)
target_link_libraries(test_apu PRIVATE lib6502)
if(UNIX)
    target_link_libraries(test_apu PRIVATE m)
endif()
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

add_executable(test_mapper_005 test_mapper_005.c)
target_link_libraries(test_mapper_005 PRIVATE lib6502)
target_include_directories(test_mapper_005 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)
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

add_executable(test_mapper_019 test_mapper_019.c)
target_link_libraries(test_mapper_019 PRIVATE lib6502)
target_include_directories(test_mapper_019 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_069 test_mapper_069.c)
target_link_libraries(test_mapper_069 PRIVATE lib6502)
target_include_directories(test_mapper_069 PRIVATE
    "${PROJECT_SOURCE_DIR}/arch/6502"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_mapper_071 test_mapper_071.c)
target_link_libraries(test_mapper_071 PRIVATE lib6502)
target_include_directories(test_mapper_071 PRIVATE
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

# ---- Blargg headless runner ------------------------------------------------
# A single binary that implements the standard Blargg result-reporting protocol
# ($6001-$6003 magic, $6000 status, $6004+ text). Runs the full NES stack
# (CPU + PPU + APU + cartridge) without SDL2.
#
# To enable CTest entries supply the path to a nes-test-roms checkout:
#   cmake -DBLARGG_TEST_ROMS_PATH=/path/to/nes-test-roms ..
add_executable(test_blargg_runner test_blargg_runner.c)
target_link_libraries(test_blargg_runner PRIVATE lib6502)
target_include_directories(test_blargg_runner PRIVATE
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
add_test(NAME mapper_005   COMMAND test_mapper_005)
add_test(NAME mapper_007   COMMAND test_mapper_007)
add_test(NAME mapper_009   COMMAND test_mapper_009)
add_test(NAME mapper_011   COMMAND test_mapper_011)
add_test(NAME mapper_066   COMMAND test_mapper_066)
add_test(NAME mapper_019   COMMAND test_mapper_019)
add_test(NAME mapper_069   COMMAND test_mapper_069)
add_test(NAME mapper_071   COMMAND test_mapper_071)
add_test(NAME cpu_nestest  COMMAND test_cpu_nestest)
add_test(NAME disasm       COMMAND test_disasm)

# ---- Blargg CTest entries (optional) --------------------------------------
if(DEFINED BLARGG_TEST_ROMS_PATH AND EXISTS "${BLARGG_TEST_ROMS_PATH}")
    set(_B "${BLARGG_TEST_ROMS_PATH}")

    # PPU VBL / NMI timing tests — issue #81
    foreach(_T
        01-vbl_basics 02-vbl_set_time 03-vbl_clear_time
        04-nmi_control 05-nmi_timing 06-suppression
        07-nmi_on_timing 08-nmi_off_timing
        09-even_odd_frames 10-even_odd_timing
    )
        set(_ROM "${_B}/ppu_vbl_nmi/rom_singles/${_T}.nes")
        if(EXISTS "${_ROM}")
            add_test(NAME "blargg_ppu_vbl_nmi_${_T}"
                     COMMAND test_blargg_runner "${_ROM}")
        endif()
    endforeach()

    # APU tests — issue #82
    # apu_test suite (newer, uses $6000 protocol)
    foreach(_T 1-len_ctr 2-len_table 3-irq_flag 4-jitter
               5-len_timing 6-irq_flag_timing 7-dmc_basics 8-dmc_rates)
        set(_ROM "${_B}/apu_test/rom_singles/${_T}.nes")
        if(EXISTS "${_ROM}")
            add_test(NAME "blargg_apu_${_T}"
                     COMMAND test_blargg_runner "${_ROM}")
        endif()
    endforeach()

    # APU reset-state tests — runner handles the $81 mid-test reset request
    foreach(_T 4015_cleared 4017_timing 4017_written
               irq_flag_cleared len_ctrs_enabled works_immediately)
        set(_ROM "${_B}/apu_reset/${_T}.nes")
        if(EXISTS "${_ROM}")
            add_test(NAME "blargg_apu_reset_${_T}"
                     COMMAND test_blargg_runner "${_ROM}")
        endif()
    endforeach()

    # Note: sprite_hit_tests_2005.10.05 and sprite_overflow_tests are 2005-era
    # ROMs that report results only via screen/beeps — they do not implement the
    # $6000 memory protocol and cannot be run headlessly with this runner.
    # Issue #83 (sprite 0 hit accuracy) must be evaluated manually until a newer
    # protocol-aware sprite test ROM is available.

    message(STATUS "Blargg tests enabled from: ${BLARGG_TEST_ROMS_PATH}")
else()
    message(STATUS "Blargg tests disabled — set -DBLARGG_TEST_ROMS_PATH=<dir> to enable")
endif()
