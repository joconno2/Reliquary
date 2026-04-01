cmake_minimum_required(VERSION 3.20)
project(Reliquary VERSION 0.2.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL2 — try pkg-config first, fall back to manually set variables (for cross-compile)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
    pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
    pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
else()
    # Cross-compile or no pkg-config — expect SDL2 vars set via -D flags
    if(NOT SDL2_INCLUDE_DIRS)
        message(FATAL_ERROR "SDL2_INCLUDE_DIRS not set. Pass -DSDL2_INCLUDE_DIRS=...")
    endif()
    if(NOT SDL2_LIBRARIES)
        message(FATAL_ERROR "SDL2_LIBRARIES not set. Pass -DSDL2_LIBRARIES=...")
    endif()
endif()

# nlohmann/json - header only, fetch if not found
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(reliquary ${SOURCES})

target_include_directories(reliquary PRIVATE
    src
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
    ${SDL2_MIXER_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
)

target_link_libraries(reliquary PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_MIXER_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    nlohmann_json::nlohmann_json
)

target_compile_options(reliquary PRIVATE
    ${SDL2_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)

# Copy assets to build directory
add_custom_command(TARGET reliquary POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:reliquary>/assets
)
