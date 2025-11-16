# Build configuration
set(CMAKE_BUILD_TYPE Debug)

# Enable full debug symbols and disable optimizations for easier debugging
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -Wall -Wextra -fno-omit-frame-pointer")


add_library(lib6502 6502.c 2c02.c nesbus.c cartridge.c mapper.c controller.c
			nes_input.c mapper_000.c mapper_001.c mapper_002.c mapper_003.c
			 debug.c )