# Generic SDL display library for emulators

# Find SDL2
find_package(SDL2 REQUIRED)

# Create display library
add_library(libdisplay STATIC
    display.c
    display.h
)

# Set include directories
target_include_directories(libdisplay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
)

# Link SDL2
target_link_libraries(libdisplay PUBLIC ${SDL2_LIBRARIES})

# Set library properties
set_target_properties(libdisplay PROPERTIES
    OUTPUT_NAME display
    VERSION 1.0.0
)
