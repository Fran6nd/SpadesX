// LuaScriptManager.h - Internal Lua scripting backend interface
//
// This header is only included within the Scripting/ source files.
// It exposes the Lua manager lifecycle and hook dispatchers that
// ScriptingAPI.c delegates to, plus helpers that LuaBindings.c uses.

#ifndef LUA_SCRIPT_MANAGER_H
#define LUA_SCRIPT_MANAGER_H

#include <Server/Scripting/ScriptingAPI.h>
#include <lua.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Lifecycle
// ============================================================================

void lua_script_manager_init(server_t* server);
void lua_script_manager_shutdown(server_t* server);
void lua_script_manager_map_load(server_t* server, const char* map_name, char** scripts, size_t count);
void lua_script_manager_map_unload(server_t* server, const char* map_name);

// ============================================================================
// Hook dispatchers (called from ScriptingAPI.c)
// ============================================================================

void lua_hook_server_init(server_t* server);
void lua_hook_server_shutdown(server_t* server);
void lua_hook_tick(server_t* server);
void lua_hook_player_connect(server_t* server, player_t* player);
void lua_hook_player_disconnect(server_t* server, player_t* player, const char* reason);
void lua_hook_player_kill(server_t* server, player_t* killer, player_t* victim,
                          uint8_t kill_reason);
void lua_hook_map_load(server_t* server, const char* map_name);
void lua_hook_map_unload(server_t* server, const char* map_name);
int  lua_hook_grenade_explode(server_t* server, player_t* player, vector3f_t position);
int  lua_hook_block_place(server_t* server, player_t* player, block_t* block);
int  lua_hook_block_destroy(server_t* server, player_t* player, uint8_t tool, block_t* block);
int  lua_hook_player_hit(server_t* server, player_t* shooter, player_t* victim,
                         uint8_t hit_type, uint8_t weapon);
int  lua_hook_color_change(server_t* server, player_t* player, uint32_t* new_color);
int  lua_hook_command(server_t* server, player_t* player, const char* command);

// ============================================================================
// Helpers used by LuaBindings.c
// ============================================================================

// Retrieve the server pointer stored in a Lua state's registry.
server_t* lua_mgr_get_server(lua_State* L);

// Register a command backed by a Lua function reference.
// func_ref must be a valid LUA_REGISTRYINDEX reference obtained via luaL_ref().
// The command is tracked internally and unregistered when the owning script
// state is closed (map unload or server shutdown).
void lua_mgr_register_command(lua_State* L, int func_ref,
                               const char* name, const char* description,
                               uint32_t permissions);

#ifdef __cplusplus
}
#endif

#endif // LUA_SCRIPT_MANAGER_H
