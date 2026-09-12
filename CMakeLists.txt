# XenonLive Launcher — the account and social front end for the XenonRecomp
# ports: sign in, friends, invites, achievements, and a Play button.
#
# C++20, SDL2, Dear ImGui (vendored, SDL_Renderer2 backend), libxlive linked
# statically from the sibling XenonLive checkout. One binary; the only runtime
# dependencies are SDL2 and libcurl, which libxlive already needs.
cmake_minimum_required(VERSION 3.20)
project(xenonlive_launcher C CXX)

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
# libxlive treats libcurl as optional (a game without it is merely offline);
# the launcher downloads releases, so here it is required.
find_package(CURL REQUIRED)

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

# miniz 3.0.2, the zip reader for the Windows release bundle. Vendored for
# the same reason, and built everywhere so the Windows path is tested here.
add_library(miniz STATIC thirdparty/miniz/miniz.c)
target_include_directories(miniz PUBLIC thirdparty/miniz)
target_compile_options(miniz PRIVATE -w)

# zstd 1.5.7's single-file decoder (BSD), for the Linux .tar.zst bundle.
add_library(zstddec STATIC thirdparty/zstd/zstddeclib.c)
target_include_directories(zstddec PUBLIC thirdparty/zstd)
target_compile_options(zstddec PRIVATE -w)

# Selawik (SIL OFL), the UI font, as bytes in the binary; and the theme
# both the launcher and the overlay draw with.
add_library(selawik STATIC thirdparty/selawik/selawik_data.cpp)
target_include_directories(selawik PUBLIC thirdparty/selawik)

add_executable(xenonlive_launcher
    common/theme.cpp
    src/main.cpp
    src/app.cpp
    src/accounts.cpp
    src/images.cpp
    src/config.cpp
    src/launch.cpp
    src/toasts.cpp
    src/catalog.cpp
    src/installer.cpp
    src/archive.cpp
    src/sha256.cpp
    src/screens/signin.cpp
    src/screens/home.cpp
    src/screens/friends.cpp
    src/screens/invites.cpp
    src/screens/achievements.cpp
    src/screens/profile.cpp
)
target_include_directories(xenonlive_launcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    # libxlive's own JSON parser and path helpers, for launcher.json. They are
    # the library's private headers, reached the way its tests reach them: the
    # launcher is the same project's code and there is no reason to carry a
    # second JSON parser next to one that is already linked in.
    ${XLIVE_ROOT}/client/src
    # stb_image.h v2.30, the PNG decoder for the tiles.
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb
)
# images.cpp instantiates stb_image, whose unused helpers are not ours.
set_source_files_properties(src/images.cpp PROPERTIES COMPILE_OPTIONS "-Wno-unused-function")
target_compile_options(xenonlive_launcher PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
target_compile_definitions(xenonlive_launcher PRIVATE
    $<$<BOOL:${WIN32}>:NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS>
)
# SDL2main before SDL2: it is a static archive whose WinMain calls into the
# DLL, and GNU ld resolves archives in order.
if(TARGET SDL2::SDL2main)
    target_link_libraries(xenonlive_launcher PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(xenonlive_launcher PRIVATE xlive::client imgui selawik miniz zstddec CURL::libcurl SDL2::SDL2)

# A release bundle carries its own lib/ (SDL2, libstdc++) beside the
# executable, found through the RPATH rather than a launcher script a player
# can bypass — the same decision the ports made (their release_package_linux.sh).
option(XENONLIVE_RELEASE "Link for a release bundle: $ORIGIN/lib RPATH, no console on Windows" OFF)
if(XENONLIVE_RELEASE)
    if(UNIX AND NOT APPLE)
        set_target_properties(xenonlive_launcher PROPERTIES
            BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib" BUILD_WITH_INSTALL_RPATH ON)
    endif()
    if(WIN32)
        set_target_properties(xenonlive_launcher PROPERTIES WIN32_EXECUTABLE ON)
        if(MINGW)
            # No libgcc/libstdc++/winpthread DLLs to ship.
            target_link_options(xenonlive_launcher PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()
endif()
