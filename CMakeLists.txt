cmake_minimum_required(VERSION 3.16)

# SpadesX
project(SpadesX LANGUAGES C)

# Find pthread
find_package(Threads REQUIRED)

# Find json-c using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSON-C REQUIRED json-c)
include_directories(${JSON-C_INCLUDE_DIRS})
link_directories(${JSON-C_LIBRARY_DIRS})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
option(CMAKE_UNITY_BUILD "Enable Unity Build" ON)

option(GIT_SUBMODULES_FETCH "Fetch the required Git submodules" ON)

# Add third party libraries
add_subdirectory(Extern)

# Add main target
add_compile_options(-Wall -Wextra -Werror -Wpedantic -Wno-error=unused-but-set-parameter -Wno-error=pedantic -std=gnu11 -fstack-protector-strong)
add_executable(SpadesX "")

target_link_libraries(SpadesX
    PRIVATE
        Server
        Util
        enet
        tomlc99
        mapvxl
        m
        ${JSON-C_LIBRARIES}
        readline
        Threads::Threads
)

add_subdirectory(Source)

set_target_properties(SpadesX Util Server
  PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION true
)

if (NOT EXISTS ${CMAKE_BINARY_DIR}/config.toml)
configure_file(${PROJECT_SOURCE_DIR}/Resources/config.toml ${CMAKE_BINARY_DIR}/config.toml COPYONLY)
endif()

# Copy Resources directory structure to build directory
# This includes the maps folder structure (Resources/maps/MapName/MapName.{vxl,toml})
if (NOT EXISTS ${CMAKE_BINARY_DIR}/Resources)
    file(COPY ${PROJECT_SOURCE_DIR}/Resources/ DESTINATION ${CMAKE_BINARY_DIR}/Resources)
endif()
