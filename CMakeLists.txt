cmake_minimum_required(VERSION 3.16)

# SpadesX
project(SpadesX LANGUAGES C)

# Find pthread
find_package(Threads REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
option(CMAKE_UNITY_BUILD "Enable Unity Build" ON)

option(GIT_SUBMODULES_FETCH "Fetch the required Git submodules" ON)

# Add third party libraries
add_subdirectory(Extern)

# Add main target
add_compile_options(-Wall -Wextra -Werror -Wpedantic -Wno-error=unused-but-set-parameter -Wno-error=pedantic -std=gnu11 -fstack-protector-strong)
add_executable(SpadesX "")

find_library(JSON_C_LIBRARY NAMES json-c REQUIRED)
find_path(JSON_C_INCLUDE_DIR NAMES json-c/json.h REQUIRED)
find_library(READLINE_LIBRARY NAMES readline REQUIRED)
find_path(READLINE_INCLUDE_DIR NAMES readline/readline.h REQUIRED)

target_include_directories(SpadesX PRIVATE ${JSON_C_INCLUDE_DIR} ${READLINE_INCLUDE_DIR})

target_link_libraries(SpadesX
    PRIVATE
        Server
        Util
        enet
        tomlc99
        mapvxl
        m
        ${JSON_C_LIBRARY}
        ${READLINE_LIBRARY}
        Threads::Threads
        lua54
)

add_subdirectory(Source)

set_target_properties(SpadesX Util Server
  PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION true
)

configure_file(${PROJECT_SOURCE_DIR}/Resources/config.toml ${CMAKE_BINARY_DIR}/config.toml COPYONLY)

# Copy Resources directory structure to build directory
# This includes the maps folder structure (Resources/maps/MapName/MapName.{vxl,toml})
if (NOT EXISTS ${CMAKE_BINARY_DIR}/Resources)
    file(COPY ${PROJECT_SOURCE_DIR}/Resources/ DESTINATION ${CMAKE_BINARY_DIR}/Resources)
endif()

# Copy scripts directory to build dir (server-wide Lua scripts)
file(COPY ${PROJECT_SOURCE_DIR}/Resources/scripts DESTINATION ${CMAKE_BINARY_DIR})

# Copy map_scripts directory to build dir (per-map Lua scripts)
file(COPY ${PROJECT_SOURCE_DIR}/Resources/map_scripts DESTINATION ${CMAKE_BINARY_DIR})
