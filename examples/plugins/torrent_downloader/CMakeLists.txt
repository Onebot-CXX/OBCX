# Torrent Downloader Plugin CMakeLists.txt

# Find required packages
find_package(nlohmann_json REQUIRED)

# Create the Torrent Downloader plugin shared library
add_library(
  torrent_downloader_plugin SHARED
  torrent_downloader_plugin.cpp
  torrent_downloader_plugin.hpp
  qbittorrent_client.cpp
  qbittorrent_client.hpp
  rclone_client.cpp
  rclone_client.hpp)

# Set the output name to match plugin loading expectations
set_target_properties(
  torrent_downloader_plugin
  PROPERTIES OUTPUT_NAME "torrent_downloader"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 20
             CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(
  torrent_downloader_plugin
  PRIVATE ${CMAKE_SOURCE_DIR}/examples ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/src)

# Link against OBCX core and required libraries
target_link_libraries(
  torrent_downloader_plugin
  PRIVATE obcx_core nlohmann_json::nlohmann_json)

# Set the output directory to build/plugins
set_target_properties(
  torrent_downloader_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                       ${CMAKE_BINARY_DIR}/plugins)
