if (GIT_SUBMODULES_FETCH)
  find_package(Git REQUIRED)

  if(NOT EXISTS libmapvxl/CMakeLists.txt OR NOT EXISTS spadesx_enet/CMakeLists.txt OR NOT EXISTS lua54/lapi.c)
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive -- ${dir}
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      COMMAND_ERROR_IS_FATAL ANY)
  endif()
endif()

add_subdirectory(libmapvxl)
add_subdirectory(spadesx_enet)
add_subdirectory(tomlc99)

# Build Lua 5.4 as a static library directly from its source tree.
# Excludes lua.c (standalone interpreter main) and onelua.c (unity build).
add_library(lua54 STATIC
    lua54/lapi.c
    lua54/lauxlib.c
    lua54/lbaselib.c
    lua54/lcode.c
    lua54/lcorolib.c
    lua54/lctype.c
    lua54/ldblib.c
    lua54/ldebug.c
    lua54/ldo.c
    lua54/ldump.c
    lua54/lfunc.c
    lua54/lgc.c
    lua54/linit.c
    lua54/liolib.c
    lua54/llex.c
    lua54/lmathlib.c
    lua54/lmem.c
    lua54/loadlib.c
    lua54/lobject.c
    lua54/lopcodes.c
    lua54/loslib.c
    lua54/lparser.c
    lua54/lstate.c
    lua54/lstring.c
    lua54/lstrlib.c
    lua54/ltable.c
    lua54/ltablib.c
    lua54/ltm.c
    lua54/lundump.c
    lua54/lutf8lib.c
    lua54/lvm.c
    lua54/lzio.c
)

target_include_directories(lua54 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua54)

# Suppress warnings in upstream Lua source
target_compile_options(lua54 PRIVATE -w)
