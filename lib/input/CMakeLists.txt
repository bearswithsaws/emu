add_library(libinput gamepad.c)

target_include_directories(libinput PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/consoles/nes
)

if(MSVC)
    target_link_libraries(libinput PRIVATE SDL2::SDL2-static)
    target_compile_definitions(libinput PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
