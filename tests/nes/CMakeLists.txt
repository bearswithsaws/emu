set(_NES_INCLUDES
    "${PROJECT_SOURCE_DIR}/consoles/nes"
    "${PROJECT_SOURCE_DIR}/consoles/nes/mappers"
    "${PROJECT_SOURCE_DIR}/cpu/6502"
    "${PROJECT_SOURCE_DIR}/lib/input"
    "${PROJECT_BINARY_DIR}"
)

add_executable(test_ppu_clock test_ppu_clock.c)
target_link_libraries(test_ppu_clock PRIVATE lib_nes)
target_include_directories(test_ppu_clock PRIVATE ${_NES_INCLUDES})

add_executable(test_apu test_apu.c)
target_link_libraries(test_apu PRIVATE lib_nes)
if(UNIX)
    target_link_libraries(test_apu PRIVATE m)
endif()
target_include_directories(test_apu PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_001 test_mapper_001.c)
target_link_libraries(test_mapper_001 PRIVATE lib_nes)
target_include_directories(test_mapper_001 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_002 test_mapper_002.c)
target_link_libraries(test_mapper_002 PRIVATE lib_nes)
target_include_directories(test_mapper_002 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_003 test_mapper_003.c)
target_link_libraries(test_mapper_003 PRIVATE lib_nes)
target_include_directories(test_mapper_003 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_004 test_mapper_004.c)
target_link_libraries(test_mapper_004 PRIVATE lib_nes)
target_include_directories(test_mapper_004 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_005 test_mapper_005.c)
target_link_libraries(test_mapper_005 PRIVATE lib_nes)
target_include_directories(test_mapper_005 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_007 test_mapper_007.c)
target_link_libraries(test_mapper_007 PRIVATE lib_nes)
target_include_directories(test_mapper_007 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_009 test_mapper_009.c)
target_link_libraries(test_mapper_009 PRIVATE lib_nes)
target_include_directories(test_mapper_009 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_011 test_mapper_011.c)
target_link_libraries(test_mapper_011 PRIVATE lib_nes)
target_include_directories(test_mapper_011 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_019 test_mapper_019.c)
target_link_libraries(test_mapper_019 PRIVATE lib_nes)
target_include_directories(test_mapper_019 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_066 test_mapper_066.c)
target_link_libraries(test_mapper_066 PRIVATE lib_nes)
target_include_directories(test_mapper_066 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_069 test_mapper_069.c)
target_link_libraries(test_mapper_069 PRIVATE lib_nes)
target_include_directories(test_mapper_069 PRIVATE ${_NES_INCLUDES})

add_executable(test_mapper_071 test_mapper_071.c)
target_link_libraries(test_mapper_071 PRIVATE lib_nes)
target_include_directories(test_mapper_071 PRIVATE ${_NES_INCLUDES})

add_executable(test_blargg_runner test_blargg_runner.c)
target_link_libraries(test_blargg_runner PRIVATE lib_nes)
target_include_directories(test_blargg_runner PRIVATE ${_NES_INCLUDES})

add_test(NAME ppu_clock  COMMAND test_ppu_clock)
add_test(NAME apu        COMMAND test_apu)
add_test(NAME mapper_001 COMMAND test_mapper_001)
add_test(NAME mapper_002 COMMAND test_mapper_002)
add_test(NAME mapper_003 COMMAND test_mapper_003)
add_test(NAME mapper_004 COMMAND test_mapper_004)
add_test(NAME mapper_005 COMMAND test_mapper_005)
add_test(NAME mapper_007 COMMAND test_mapper_007)
add_test(NAME mapper_009 COMMAND test_mapper_009)
add_test(NAME mapper_011 COMMAND test_mapper_011)
add_test(NAME mapper_019 COMMAND test_mapper_019)
add_test(NAME mapper_066 COMMAND test_mapper_066)
add_test(NAME mapper_069 COMMAND test_mapper_069)
add_test(NAME mapper_071 COMMAND test_mapper_071)

# ---- Blargg headless runner ------------------------------------------------
if(DEFINED BLARGG_TEST_ROMS_PATH AND EXISTS "${BLARGG_TEST_ROMS_PATH}")
    set(_B "${BLARGG_TEST_ROMS_PATH}")

    foreach(_T
        01-vbl_basics 02-vbl_set_time 03-vbl_clear_time
        04-nmi_control 05-nmi_timing 06-suppression
        07-nmi_on_timing 08-nmi_off_timing
        09-even_odd_frames 10-even_odd_timing
    )
        set(_ROM "${_B}/ppu_vbl_nmi/rom_singles/${_T}.nes")
        if(EXISTS "${_ROM}")
            add_test(NAME "blargg_ppu_vbl_nmi_${_T}" COMMAND test_blargg_runner "${_ROM}")
        endif()
    endforeach()

    foreach(_T 1-len_ctr 2-len_table 3-irq_flag 4-jitter
               5-len_timing 6-irq_flag_timing 7-dmc_basics 8-dmc_rates)
        set(_ROM "${_B}/apu_test/rom_singles/${_T}.nes")
        if(EXISTS "${_ROM}")
            add_test(NAME "blargg_apu_${_T}" COMMAND test_blargg_runner "${_ROM}")
        endif()
    endforeach()

    foreach(_T 4015_cleared 4017_timing 4017_written
               irq_flag_cleared len_ctrs_enabled works_immediately)
        set(_ROM "${_B}/apu_reset/${_T}.nes")
        if(EXISTS "${_ROM}")
            add_test(NAME "blargg_apu_reset_${_T}" COMMAND test_blargg_runner "${_ROM}")
        endif()
    endforeach()

    message(STATUS "Blargg tests enabled from: ${BLARGG_TEST_ROMS_PATH}")
else()
    message(STATUS "Blargg tests disabled — set -DBLARGG_TEST_ROMS_PATH=<dir> to enable")
endif()
