cmake_minimum_required(VERSION 3.16)
project(TowerDefense CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(towerdefense
    src/main.cpp
    src/Game.cpp
    src/Config.cpp
    src/AudioEngine.cpp
)

# ============================================================
#  Emscripten (WebAssembly) ビルド
#  使い方: emcmake cmake -B build_web && cmake --build build_web
# ============================================================
if (EMSCRIPTEN)
    set_target_properties(towerdefense PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html")

    # コンパイル時にもSDLポートのヘッダを使うよう指定する（これが必須）
    target_compile_options(towerdefense PRIVATE
        -O2
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sUSE_SDL_TTF=2"
    )
    target_link_options(towerdefense PRIVATE
        -sUSE_SDL=2
        -sUSE_SDL_TTF=2
        -sASYNCIFY
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sFORCE_FILESYSTEM=1
        "--preload-file=${CMAKE_SOURCE_DIR}/assets@/assets"
    )
    add_custom_command(TARGET towerdefense POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/docs
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_BINARY_DIR}/index.html
                ${CMAKE_BINARY_DIR}/index.js
                ${CMAKE_BINARY_DIR}/index.wasm
                ${CMAKE_BINARY_DIR}/index.data
                ${CMAKE_SOURCE_DIR}/docs/
        COMMENT "Copying web build to docs/"
    )
    return()
endif()

# ============================================================
#  ネイティブ (macOS / Windows / Linux)
# ============================================================
find_package(SDL2 QUIET)
find_package(SDL2_ttf QUIET)

if (TARGET SDL2::SDL2 AND TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(towerdefense PRIVATE SDL2::SDL2 SDL2_ttf::SDL2_ttf)
    if (TARGET SDL2::SDL2main)
        target_link_libraries(towerdefense PRIVATE SDL2::SDL2main)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2    REQUIRED sdl2)
    pkg_check_modules(SDL2TTF REQUIRED SDL2_ttf)
    target_include_directories(towerdefense PRIVATE
        ${SDL2_INCLUDE_DIRS} ${SDL2TTF_INCLUDE_DIRS})
    target_link_libraries(towerdefense PRIVATE
        ${SDL2_LIBRARIES} ${SDL2TTF_LIBRARIES})
endif()

if (MSVC)
    target_compile_options(towerdefense PRIVATE /W4 /utf-8)
else()
    target_compile_options(towerdefense PRIVATE -Wall -Wextra)
endif()

option(MAC_APP "Build as a macOS .app bundle" OFF)
if (APPLE AND MAC_APP)
    set_target_properties(towerdefense PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Tower Defense"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.portfolio.towerdefense"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        OUTPUT_NAME "TowerDefense")
endif()
