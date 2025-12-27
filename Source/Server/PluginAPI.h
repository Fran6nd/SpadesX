// PluginAPI.h - SpadesX Plugin API
// This header defines the interface that plugins can use to interact with SpadesX.
// Plugins are loaded as shared libraries (.dll/.so/.dylib) at server startup.

#ifndef SPADESX_PLUGIN_API_H
#define SPADESX_PLUGIN_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct server server_t;
typedef struct player player_t;
typedef struct map map_t;

// Import existing types from the codebase
// These are defined in Util/Types.h and Util/Enums.h
#ifndef TYPES_H
typedef union color {
    struct {
        uint8_t b, g, r, a;
    };
    uint8_t  arr[4];
    uint32_t raw;
} color_t;

typedef struct vector3f {
    float x, y, z;
} vector3f_t;

typedef struct vector3i {
    int x, y, z;
} vector3i_t;
#endif

// Tool types - these match the values in Util/Enums.h
#define TOOL_SPADE    0
#define TOOL_BLOCK    1
#define TOOL_GUN      2
#define TOOL_GRENADE  3

// ============================================================================
// PLUGIN METADATA
// ============================================================================

#define SPADESX_PLUGIN_API_VERSION 1

// Plugin information structure - must be exported by every plugin
typedef struct {
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    uint32_t    api_version;  // Must match SPADESX_PLUGIN_API_VERSION
} plugin_info_t;

// ============================================================================
// PLUGIN-SPECIFIC TYPES
// ============================================================================

// Team structure for plugins
typedef struct {
    uint8_t id;        // 0 or 1
    char    name[11];
    uint32_t color;    // Color as raw uint32
} plugin_team_t;

// Block structure for plugins
typedef struct {
    int32_t x, y, z;
    uint32_t color;    // Color as raw uint32
} block_t;

// ============================================================================
// EVENT RETURN CODES
// ============================================================================

// Return codes for event handlers
#define PLUGIN_ALLOW    1  // Allow the action to proceed
#define PLUGIN_DENY     0  // Deny/cancel the action

// ============================================================================
// PLUGIN API INTERFACE
// ============================================================================

// The API interface provided to plugins
// This structure contains function pointers to interact with the server
typedef struct plugin_api {
    // ========================================================================
    // PLAYER FUNCTIONS
    // ========================================================================

    // Get player by ID (returns NULL if not found)
    player_t* (*get_player)(server_t* server, uint8_t player_id);

    // Get player's name
    const char* (*player_get_name)(player_t* player);

    // Get player's team
    plugin_team_t (*player_get_team)(server_t* server, player_t* player);

    // Get player's current tool
    uint8_t (*player_get_tool)(player_t* player);

    // Get player's block count
    uint8_t (*player_get_blocks)(player_t* player);

    // Get player's grenade count
    uint8_t (*player_get_grenades)(player_t* player);

    // Get player's current color
    uint32_t (*player_get_color)(player_t* player);

    // Set player's color
    void (*player_set_color)(player_t* player, uint32_t color);

    // Restock player (50 blocks, 3 grenades)
    void (*player_restock)(player_t* player);

    // Send a notice/message to a specific player
    void (*player_send_notice)(player_t* player, const char* message);

    // Kill a player
    void (*player_kill)(player_t* player);

    // Set player HP (0-100)
    void (*player_set_hp)(player_t* player, uint8_t hp);

    // Get player HP
    uint8_t (*player_get_hp)(player_t* player);

    // Get player position
    vector3f_t (*player_get_position)(player_t* player);

    // Set player position
    void (*player_set_position)(player_t* player, vector3f_t position);

    // ========================================================================
    // MAP FUNCTIONS
    // ========================================================================

    // Get the map
    map_t* (*get_map)(server_t* server);

    // Get block color at position (returns 0 if no block)
    uint32_t (*map_get_block)(map_t* map, int32_t x, int32_t y, int32_t z);

    // Set block at position and notify all players
    void (*map_set_block)(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color);

    // Remove block at position and notify all players
    void (*map_remove_block)(server_t* server, int32_t x, int32_t y, int32_t z);

    // Find the topmost solid block at (x, y)
    int32_t (*map_find_top_block)(map_t* map, int32_t x, int32_t y);

    // Check if position is valid (within map bounds)
    int (*map_is_valid_pos)(map_t* map, int32_t x, int32_t y, int32_t z);

    // ========================================================================
    // INIT API (only available during on_server_init)
    // ========================================================================

    // Add a colored block during initialization (no network updates)
    void (*init_add_block)(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color);

    // Set intel position (team_id: 0 or 1)
    void (*init_set_intel_position)(server_t* server, uint8_t team_id, int32_t x, int32_t y, int32_t z);

    // ========================================================================
    // SERVER FUNCTIONS
    // ========================================================================

    // Broadcast a message to all players
    void (*broadcast_message)(server_t* server, const char* message);

    // Register a custom command
    // Returns 1 on success, 0 on failure
    int (*register_command)(
        server_t* server,
        const char* command_name,
        const char* description,
        void (*handler)(server_t* server, player_t* player, const char* args),
        uint32_t required_permissions
    );

} plugin_api_t;

// ============================================================================
// PLUGIN LIFECYCLE FUNCTIONS
// ============================================================================

// These functions must be exported by the plugin
// The plugin loader will call them at appropriate times

// Called when the plugin is loaded
// Return 0 on success, non-zero on failure (plugin will be unloaded)
typedef int (*plugin_init_fn)(server_t* server, const plugin_api_t* api);

// Called when the plugin is unloaded
typedef void (*plugin_shutdown_fn)(server_t* server);

// ============================================================================
// PLUGIN EVENT HANDLERS (Optional)
// ============================================================================

// All event handlers are optional - only export the ones you need

// Called during server initialization, before players join
// Use init_* functions from the API to set up the map
typedef void (*plugin_on_server_init_fn)(server_t* server, const plugin_api_t* api);

// Called when server is shutting down
typedef void (*plugin_on_server_shutdown_fn)(server_t* server);

// Called when a player attempts to destroy a block
// Return PLUGIN_ALLOW to allow, PLUGIN_DENY to prevent
typedef int (*plugin_on_block_destroy_fn)(
    server_t* server,
    player_t* player,
    uint8_t tool,
    block_t* block
);

// Called when a player attempts to place a block
// Block color can be modified
// Return PLUGIN_ALLOW to allow, PLUGIN_DENY to prevent
typedef int (*plugin_on_block_place_fn)(
    server_t* server,
    player_t* player,
    block_t* block  // Can modify block->color
);

// Called when a player sends a command
// Return PLUGIN_ALLOW if command was handled, PLUGIN_DENY if not
typedef int (*plugin_on_command_fn)(
    server_t* server,
    player_t* player,
    const char* command
);

// Called when a player connects
typedef void (*plugin_on_player_connect_fn)(
    server_t* server,
    player_t* player
);

// Called when a player disconnects
typedef void (*plugin_on_player_disconnect_fn)(
    server_t* server,
    player_t* player,
    const char* reason
);

// Called when a grenade explodes
typedef void (*plugin_on_grenade_explode_fn)(
    server_t* server,
    player_t* player,
    vector3f_t position
);

// Called every server tick (60 times per second)
typedef void (*plugin_on_tick_fn)(server_t* server);

// ============================================================================
// PLUGIN EXPORT MACROS
// ============================================================================

// Use these macros to export your plugin functions

#ifdef _WIN32
    #define PLUGIN_EXPORT __declspec(dllexport)
#else
    #define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// Export plugin info
#define PLUGIN_INFO(name, version, author, description) \
    PLUGIN_EXPORT plugin_info_t spadesx_plugin_info = { \
        .name = name, \
        .version = version, \
        .author = author, \
        .description = description, \
        .api_version = SPADESX_PLUGIN_API_VERSION \
    };

// Export lifecycle functions
// Note: Don't use these macros - export functions directly with PLUGIN_EXPORT
// These are deprecated and cause bus errors on some platforms
#define PLUGIN_INIT(func)     PLUGIN_EXPORT plugin_init_fn spadesx_plugin_init = func;
#define PLUGIN_SHUTDOWN(func) PLUGIN_EXPORT plugin_shutdown_fn spadesx_plugin_shutdown = func;

// Export event handlers
#define PLUGIN_ON_SERVER_INIT(func)       PLUGIN_EXPORT plugin_on_server_init_fn spadesx_plugin_on_server_init = func;
#define PLUGIN_ON_SERVER_SHUTDOWN(func)   PLUGIN_EXPORT plugin_on_server_shutdown_fn spadesx_plugin_on_server_shutdown = func;
#define PLUGIN_ON_BLOCK_DESTROY(func)     PLUGIN_EXPORT plugin_on_block_destroy_fn spadesx_plugin_on_block_destroy = func;
#define PLUGIN_ON_BLOCK_PLACE(func)       PLUGIN_EXPORT plugin_on_block_place_fn spadesx_plugin_on_block_place = func;
#define PLUGIN_ON_COMMAND(func)           PLUGIN_EXPORT plugin_on_command_fn spadesx_plugin_on_command = func;
#define PLUGIN_ON_PLAYER_CONNECT(func)    PLUGIN_EXPORT plugin_on_player_connect_fn spadesx_plugin_on_player_connect = func;
#define PLUGIN_ON_PLAYER_DISCONNECT(func) PLUGIN_EXPORT plugin_on_player_disconnect_fn spadesx_plugin_on_player_disconnect = func;
#define PLUGIN_ON_GRENADE_EXPLODE(func)   PLUGIN_EXPORT plugin_on_grenade_explode_fn spadesx_plugin_on_grenade_explode = func;
#define PLUGIN_ON_TICK(func)              PLUGIN_EXPORT plugin_on_tick_fn spadesx_plugin_on_tick = func;

#ifdef __cplusplus
}
#endif

#endif // SPADESX_PLUGIN_API_H
