cmake_minimum_required(VERSION 3.21)
project(qdm_torrent_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LibtorrentRasterbar CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(qdm-torrent-engine main.cpp)
target_link_libraries(qdm-torrent-engine PRIVATE
    LibtorrentRasterbar::torrent-rasterbar
    nlohmann_json::nlohmann_json
)
if(MSVC)
    target_compile_options(qdm-torrent-engine PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(qdm-torrent-engine PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
