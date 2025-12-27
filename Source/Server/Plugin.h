// Plugin.h - Plugin management system for SpadesX
// Handles loading, unloading, and managing binary plugins

#ifndef SPADESX_PLUGIN_H
#define SPADESX_PLUGIN_H

#include "PluginAPI.h"
#include "Structs/ServerStruct.h"

#ifdef __cplusplus
extern "C" {
#endif

// Plugin structure - internal use
typedef struct plugin {
    void* handle;  // dlopen/LoadLibrary handle
    char  name[64];
    char  version[16];
    char  path[256];

    // Plugin functions
    plugin_init_fn              init;
    plugin_shutdown_fn          shutdown;
    plugin_on_server_init_fn    on_server_init;
    plugin_on_server_shutdown_fn on_server_shutdown;
    plugin_on_block_destroy_fn  on_block_destroy;
    plugin_on_block_place_fn    on_block_place;
    plugin_on_command_fn        on_command;
    plugin_on_player_connect_fn on_player_connect;
    plugin_on_player_disconnect_fn on_player_disconnect;
    plugin_on_grenade_explode_fn on_grenade_explode;
    plugin_on_tick_fn           on_tick;

    struct plugin* next;  // Linked list
} plugin_t;

// ============================================================================
// PLUGIN SYSTEM FUNCTIONS
// ============================================================================

// Initialize the plugin system
void plugin_system_init(server_t* server);

// Shutdown the plugin system and unload all plugins
void plugin_system_shutdown(server_t* server);

// Load a single plugin from a file path
// Returns 0 on success, non-zero on failure
int plugin_load(server_t* server, const char* path);

// Unload a single plugin
void plugin_unload(server_t* server, plugin_t* plugin);

// Get the global plugin API instance
const plugin_api_t* plugin_get_api(void);

// ============================================================================
// PLUGIN EVENT DISPATCHERS
// ============================================================================

// These functions dispatch events to all loaded plugins
// They should be called from the appropriate game code locations

void plugin_dispatch_server_init(server_t* server);
void plugin_dispatch_server_shutdown(server_t* server);

int plugin_dispatch_block_destroy(server_t* server, player_t* player, uint8_t tool, block_t* block);
int plugin_dispatch_block_place(server_t* server, player_t* player, block_t* block);
int plugin_dispatch_command(server_t* server, player_t* player, const char* command);

void plugin_dispatch_player_connect(server_t* server, player_t* player);
void plugin_dispatch_player_disconnect(server_t* server, player_t* player, const char* reason);
void plugin_dispatch_grenade_explode(server_t* server, player_t* player, vector3f_t position);
void plugin_dispatch_tick(server_t* server);

#ifdef __cplusplus
}
#endif

#endif // SPADESX_PLUGIN_H
