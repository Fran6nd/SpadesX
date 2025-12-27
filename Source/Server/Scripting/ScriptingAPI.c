// ScriptingAPI.c - Dispatches to the Lua 5.4 scripting backend.
//
// All server code calls the scripting_* functions declared in ScriptingAPI.h.
// This file is the only place that knows about the Lua backend; everything
// above this layer is backend-agnostic.

#include <Server/Scripting/ScriptingAPI.h>
#include <Server/Scripting/Lua/LuaScriptManager.h>

void scripting_init(server_t* server)
{
    lua_script_manager_init(server);
}

void scripting_shutdown(server_t* server)
{
    lua_script_manager_shutdown(server);
}

void scripting_map_load(server_t* server, const char* map_name, char** scripts, size_t count)
{
    lua_script_manager_map_load(server, map_name, scripts, count);
    // Fire on_map_load after the map script has been loaded (global code ran).
    lua_hook_map_load(server, map_name);
}

void scripting_map_unload(server_t* server, const char* map_name)
{
    // Fire on_map_unload before closing the map state so scripts can clean up.
    lua_hook_map_unload(server, map_name);
    lua_script_manager_map_unload(server, map_name);
}

void scripting_on_server_init(server_t* server)
{
    lua_hook_server_init(server);
}

void scripting_on_server_shutdown(server_t* server)
{
    lua_hook_server_shutdown(server);
}

void scripting_on_tick(server_t* server)
{
    lua_hook_tick(server);
}

void scripting_on_player_connect(server_t* server, player_t* player)
{
    lua_hook_player_connect(server, player);
}

void scripting_on_player_disconnect(server_t* server, player_t* player, const char* reason)
{
    lua_hook_player_disconnect(server, player, reason);
}

void scripting_on_grenade_explode(server_t* server, player_t* player, vector3f_t position)
{
    lua_hook_grenade_explode(server, player, position);
}

int scripting_on_block_place(server_t* server, player_t* player, block_t* block)
{
    return lua_hook_block_place(server, player, block);
}

int scripting_on_block_destroy(server_t* server, player_t* player, uint8_t tool, block_t* block)
{
    return lua_hook_block_destroy(server, player, tool, block);
}

int scripting_on_player_hit(server_t* server, player_t* shooter, player_t* victim,
                            uint8_t hit_type, uint8_t weapon)
{
    return lua_hook_player_hit(server, shooter, victim, hit_type, weapon);
}

int scripting_on_color_change(server_t* server, player_t* player, uint32_t* new_color)
{
    return lua_hook_color_change(server, player, new_color);
}

int scripting_on_command(server_t* server, player_t* player, const char* command)
{
    return lua_hook_command(server, player, command);
}
