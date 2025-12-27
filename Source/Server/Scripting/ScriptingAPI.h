// ScriptingAPI.h - Unified scripting interface for SpadesX
//
// This header defines all hooks and lifecycle functions that server code
// uses to interact with the scripting layer. The implementation backend
// (Lua 5.4) is isolated in Scripting/Lua/ — server code only includes
// this header.

#ifndef SCRIPTING_API_H
#define SCRIPTING_API_H

#include <stdint.h>
#include <Util/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations — full definitions in their respective struct headers
typedef struct server server_t;
typedef struct player player_t;

// Block position and color, used by block hooks.
// Defined here because it belongs to the scripting interface contract.
typedef struct block {
    int32_t  x, y, z;
    uint32_t color;  // packed as 0x00RRGGBB (r at byte 2, g at byte 1, b at byte 0)
} block_t;

// Return values for deny-type hooks (block place/destroy, player hit, color change).
// SCRIPTING_ALLOW (0) — let the action proceed.
// SCRIPTING_DENY  (1) — cancel the action.
#define SCRIPTING_ALLOW 0
#define SCRIPTING_DENY  1

// Return values for the command hook.
// SCRIPTING_CMD_PASS    (0) — no script handled this command.
// SCRIPTING_CMD_HANDLED (1) — a script consumed the command.
#define SCRIPTING_CMD_PASS    0
#define SCRIPTING_CMD_HANDLED 1

// ============================================================================
// Scripting system lifecycle
// ============================================================================

// Initialize the scripting system and load server-wide scripts from scripts/.
// Call once, early in server_start(), after the command system is ready.
void scripting_init(server_t* server);

// Shut down the scripting system, unregister all script-registered commands,
// and release all resources. Call once during server shutdown.
void scripting_shutdown(server_t* server);

// Load per-map scripts (may be NULL/0 for no scripts).
// Call after the map and gamemode have been initialized.
void scripting_map_load(server_t* server, const char* map_name, char** scripts, size_t count);

// Unload the current per-map script, unregister its commands, and close its
// Lua state. Call before loading the next map.
void scripting_map_unload(server_t* server, const char* map_name);

// ============================================================================
// Event hooks — called from server game code
// ============================================================================

void scripting_on_server_init(server_t* server);
void scripting_on_server_shutdown(server_t* server);
void scripting_on_tick(server_t* server);
void scripting_on_player_connect(server_t* server, player_t* player);
void scripting_on_player_disconnect(server_t* server, player_t* player, const char* reason);
void scripting_on_grenade_explode(server_t* server, player_t* player, vector3f_t position);

// Deny-type hooks — return SCRIPTING_DENY to cancel the action.
// on_block_place may also have block->color modified by a script.
// on_color_change may also have *new_color modified by a script.
int scripting_on_block_place(server_t* server, player_t* player, block_t* block);
int scripting_on_block_destroy(server_t* server, player_t* player, uint8_t tool, block_t* block);
int scripting_on_player_hit(server_t* server, player_t* shooter, player_t* victim,
                            uint8_t hit_type, uint8_t weapon);
int scripting_on_color_change(server_t* server, player_t* player, uint32_t* new_color);

// Command hook — return SCRIPTING_CMD_HANDLED if a script consumed the command.
int scripting_on_command(server_t* server, player_t* player, const char* command);

#ifdef __cplusplus
}
#endif

#endif // SCRIPTING_API_H
