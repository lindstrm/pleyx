cmake_minimum_required(VERSION 3.16)
project(pleyx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch dependencies
include(FetchContent)

# JSON library
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)


# Main executable
add_executable(pleyx WIN32
    src/main.cpp
    src/plex.cpp
    src/plex.h
    src/discord_ipc.cpp
    src/discord_ipc.h
    src/discord.cpp
    src/discord.h
    src/config.cpp
    src/config.h
    src/image_cache.cpp
    src/image_cache.h
    src/tray_icon.cpp
    src/tray_icon.h
    src/resource.h
    src/resources.rc
)

target_include_directories(pleyx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(pleyx PRIVATE
    nlohmann_json::nlohmann_json
)

# Windows-specific
if(WIN32)
    target_link_libraries(pleyx PRIVATE ws2_32 winhttp shell32 gdiplus)
    target_compile_definitions(pleyx PRIVATE _WIN32_WINNT=0x0601 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
