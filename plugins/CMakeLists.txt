# CMakeLists.txt for SpadesX plugins

cmake_minimum_required(VERSION 3.10)
project(SpadesX_Plugins)

# Include the plugin API header
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Source/Server)

# Build the example gamemode plugin as a shared library
add_library(example_gamemode SHARED example_gamemode.c)

# Set compile options for proper symbol export
target_compile_options(example_gamemode PRIVATE -fvisibility=default)

# Set properties
set_target_properties(example_gamemode PROPERTIES
    PREFIX ""  # Remove 'lib' prefix on Unix
    POSITION_INDEPENDENT_CODE ON
)

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_compile_options(example_gamemode PRIVATE -fPIC)
endif()

# Set output directory to build/plugins (where the server loads from)
set_target_properties(example_gamemode PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# Also copy to source plugins directory for development
add_custom_command(TARGET example_gamemode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:example_gamemode>
        "${CMAKE_CURRENT_SOURCE_DIR}/$<TARGET_FILE_NAME:example_gamemode>"
)

# Installation (optional)
install(TARGETS example_gamemode
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
