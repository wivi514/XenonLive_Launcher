# xlive_overlay — the in-game overlay a XenonRecomp port links.
#
# Consumed as a sibling checkout, the way the ports consume libxlive:
#
#     add_subdirectory($ENV{HOME}/GithubRepo/XenonLive_Launcher/overlay xlive_overlay)
#     target_link_libraries(cw_runtime PRIVATE xlive::overlay)
#
# It brings Dear ImGui (vendored beside it) with the Vulkan backend. The port
# supplies SDL2, Vulkan and xlive::client, all of which it already links.
cmake_minimum_required(VERSION 3.20)
project(xlive_overlay CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(XLIVE_OVERLAY_IMGUI "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui")

find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)

add_library(xlive_overlay STATIC
    src/overlay.cpp
    ${XLIVE_OVERLAY_IMGUI}/imgui.cpp
    ${XLIVE_OVERLAY_IMGUI}/imgui_draw.cpp
    ${XLIVE_OVERLAY_IMGUI}/imgui_tables.cpp
    ${XLIVE_OVERLAY_IMGUI}/imgui_widgets.cpp
    ${XLIVE_OVERLAY_IMGUI}/backends/imgui_impl_vulkan.cpp
    # For its key table only; its NewFrame is never called (see overlay.cpp).
    ${XLIVE_OVERLAY_IMGUI}/backends/imgui_impl_sdl2.cpp
)
add_library(xlive::overlay ALIAS xlive_overlay)
target_include_directories(xlive_overlay
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${XLIVE_OVERLAY_IMGUI} ${XLIVE_OVERLAY_IMGUI}/backends
)
target_link_libraries(xlive_overlay PUBLIC Vulkan::Vulkan SDL2::SDL2 xlive::client)
# Warnings for our file; none for ImGui's.
set_source_files_properties(src/overlay.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wno-missing-field-initializers")
set_source_files_properties(
    ${XLIVE_OVERLAY_IMGUI}/imgui.cpp ${XLIVE_OVERLAY_IMGUI}/imgui_draw.cpp
    ${XLIVE_OVERLAY_IMGUI}/imgui_tables.cpp ${XLIVE_OVERLAY_IMGUI}/imgui_widgets.cpp
    ${XLIVE_OVERLAY_IMGUI}/backends/imgui_impl_vulkan.cpp
    ${XLIVE_OVERLAY_IMGUI}/backends/imgui_impl_sdl2.cpp
    PROPERTIES COMPILE_OPTIONS "-w")
