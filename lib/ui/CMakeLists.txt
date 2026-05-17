set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)

add_library(libui STATIC
    ui.cpp
    ui.h
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(libui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)

target_include_directories(libui PRIVATE
    ${PROJECT_SOURCE_DIR}/lib/display
)

target_link_libraries(libui PUBLIC libdisplay ${SDL2_LIBRARIES})

set_target_properties(libui PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
