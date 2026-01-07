# Group Member Backup Plugin

add_library(group_member_backup_plugin SHARED
    group_member_backup_plugin.cpp
    group_member_backup_plugin.hpp
)

# Set the output name to match plugin loading expectations
set_target_properties(
    group_member_backup_plugin
    PROPERTIES OUTPUT_NAME "group_member_backup"
               PREFIX "" # No lib prefix
               POSITION_INDEPENDENT_CODE ON
               CXX_STANDARD 26
               CXX_STANDARD_REQUIRED ON
)

# Include directories
target_include_directories(group_member_backup_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/examples
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link against OBCX core
target_link_libraries(group_member_backup_plugin
    PRIVATE obcx_core
)

# Set the output directory to build/plugins
set_target_properties(group_member_backup_plugin PROPERTIES 
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
)
