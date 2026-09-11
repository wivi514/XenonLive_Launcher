# XenonLive Launcher — the account and social front end for the XenonRecomp
# ports: sign in, friends, invites, achievements, and a Play button.
#
# C++20, SDL2, Dear ImGui (vendored, SDL_Renderer2 backend), libxlive linked
# statically from the sibling XenonLive checkout. One binary; the only runtime
# dependencies are SDL2 and libcurl, which libxlive already needs.
cmake_minimum_required(VERSION 3.20)
project(xenonlive_launcher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The sibling checkout, the same convention the ports use for XenonRecomp.
set(XLIVE_ROOT "$ENV{HOME}/GithubRepo/XenonLive" CACHE PATH "XenonLive source root")
if(NOT EXISTS "${XLIVE_ROOT}/client/CMakeLists.txt")
    message(FATAL_ERROR
        "libxlive not found at ${XLIVE_ROOT}/client. Check out "
        "github.com/wivi514/XenonLive beside this repo, or pass -DXLIVE_ROOT=.")
endif()
set(XLIVE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${XLIVE_ROOT}/client" "${CMAKE_CURRENT_BINARY_DIR}/xlive")

find_package(SDL2 REQUIRED)

# Dear ImGui 1.91.9b, vendored under thirdparty/ rather than fetched: a port
# pins this checkout for a decade, and code we own beats a module that may not
# resolve in 2035. tools/fetch_imgui.sh re-creates the directory if it is lost.
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui")
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2)
# ImGui's own warnings are not ours to fix.
target_compile_options(imgui PRIVATE -w)

add_executable(xenonlive_launcher
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/launch.cpp
    src/toasts.cpp
    src/screens/signin.cpp
    src/screens/home.cpp
    src/screens/friends.cpp
    src/screens/invites.cpp
    src/screens/achievements.cpp
)
target_include_directories(xenonlive_launcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # libxlive's own JSON parser and path helpers, for launcher.json. They are
    # the library's private headers, reached the way its tests reach them: the
    # launcher is the same project's code and there is no reason to carry a
    # second JSON parser next to one that is already linked in.
    ${XLIVE_ROOT}/client/src
)
target_compile_options(xenonlive_launcher PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
target_compile_definitions(xenonlive_launcher PRIVATE
    $<$<BOOL:${WIN32}>:NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS>
)
target_link_libraries(xenonlive_launcher PRIVATE xlive::client imgui SDL2::SDL2)
if(TARGET SDL2::SDL2main)
    target_link_libraries(xenonlive_launcher PRIVATE SDL2::SDL2main)
endif()
