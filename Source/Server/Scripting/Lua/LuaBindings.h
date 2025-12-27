// LuaBindings.h - Lua API registration
//
// Exposes a single function that registers all Lua API modules
// (player, map, server, log) into a Lua state.

#ifndef LUA_BINDINGS_H
#define LUA_BINDINGS_H

#include <lua.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register all scripting API modules into the given Lua state.
// Call this once after luaL_openlibs() and before loading any scripts.
void lua_bindings_register(lua_State* L);

#ifdef __cplusplus
}
#endif

#endif // LUA_BINDINGS_H
