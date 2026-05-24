add_library(lib_cpu_6502 6502.c disasm.c)

target_include_directories(lib_cpu_6502 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/consoles/nes
    ${PROJECT_SOURCE_DIR}/consoles/nes/mappers
)
