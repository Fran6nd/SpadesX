// LuaBindings.h - Lua API registration
//
// Exposes a single function that registers all Lua API modules
// (player, map, server, log) into a Lua state.

#ifndef LUA_BINDINGS_H
#define LUA_BINDINGS_H

#include <lua.h>
#include <Server/Structs/ServerStruct.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register all scripting API modules into the given Lua state.
// server is injected as upvalue #1 into every function in player_lib,
// bot_lib, map_lib, and server_lib.
// Call this once after luaL_openlibs() and before loading any scripts.
void lua_bindings_register(lua_State* L, server_t* server);

#ifdef __cplusplus
}
#endif

#endif // LUA_BINDINGS_H
