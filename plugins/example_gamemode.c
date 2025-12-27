// example_gamemode.c - Example SpadesX plugin
// This plugin demonstrates the same functionality as the Lua script from GameMode.lua
//
// Features:
// - Prevents destroying the Babel platform (206-306, 240-272, z=0-2)
// - Prevents teams from destroying their own towers (except with spade)
// - Forces players to build in their team color
// - Auto-restocks players when blocks < 10
// - Adds /restock command
// - Initializes map with snow and platform

#include "../Source/Server/PluginAPI.h"
#include <stdio.h>
#include <string.h>

// Plugin metadata
PLUGIN_EXPORT plugin_info_t spadesx_plugin_info = {
    .name = "Example Gamemode",
    .version = "1.0.0",
    .author = "SpadesX Team",
    .description = "Babel-style gamemode with platform protection",
    .api_version = SPADESX_PLUGIN_API_VERSION
};

// Store the API for later use
static const plugin_api_t* api = NULL;
static const char* PLUGIN_NAME = "Example Gamemode";

// Track blocks above players
typedef struct {
    uint8_t player_id;
    int32_t block_x;
    int32_t block_y;
    int32_t block_z;
    uint8_t has_block;
} player_block_tracker_t;

static player_block_tracker_t player_blocks[32]; // Max 32 players
static int tick_counter = 0; // For debug logging

// ============================================================================
// PLUGIN LIFECYCLE
// ============================================================================

PLUGIN_EXPORT int spadesx_plugin_init(server_t* server, const plugin_api_t* plugin_api)
{
    (void)server;  // Unused
    api = plugin_api;
    api->log_info(PLUGIN_NAME, "Initializing...");
    api->log_debug(PLUGIN_NAME, "API pointer: %p", (void*)plugin_api);

    // Initialize player block tracking
    for (int i = 0; i < 32; i++) {
        player_blocks[i].player_id = i;
        player_blocks[i].has_block = 0;
    }

    api->log_info(PLUGIN_NAME, "Loaded successfully! Player trail feature enabled.");
    return 0;
}

PLUGIN_EXPORT void spadesx_plugin_shutdown(server_t* server)
{
    (void)server;  // Unused
    api->log_info(PLUGIN_NAME, "Shutting down");
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

// Server initialization - set up the map
PLUGIN_EXPORT void spadesx_plugin_on_server_init(server_t* server, const plugin_api_t* plugin_api)
{
    api->log_info(PLUGIN_NAME, "Initializing map...");

    map_t* map = plugin_api->get_map(server);

    if (!map) {
        api->log_error(PLUGIN_NAME, "Map is NULL!");
        return;
    }

    // Create Babel platform (cyan) - smaller test first
    api->log_info(PLUGIN_NAME, "Creating platform...");
    for (int32_t x = 206; x <= 306; x++) {
        for (int32_t y = 240; y <= 272; y++) {
            plugin_api->init_add_block(server, x, y, 1, 0xFF00FFFF);  // Cyan
        }
    }

    // Set intel positions on top of platform
    api->log_info(PLUGIN_NAME, "Setting intel positions...");
    int32_t intel_z = plugin_api->map_find_top_block(map, 255, 255);
    plugin_api->init_set_intel_position(server, 0, 255, 255, intel_z);
    plugin_api->init_set_intel_position(server, 1, 255, 255, intel_z);

    api->log_info(PLUGIN_NAME, "Map initialization complete!");
}

// Block destruction check
PLUGIN_EXPORT int spadesx_plugin_on_block_destroy(server_t* server, player_t* player, uint8_t tool, block_t* block)
{
    int32_t x = block->x;
    int32_t y = block->y;
    int32_t z = block->z;

    // Prevent destroying the Babel platform
    int is_platform =
        ((x >= 206 && x <= 306) && (y >= 240 && y <= 272) && (z == 2 || z == 0)) ||
        ((x >= 205 && x <= 307) && (y >= 239 && y <= 273) && (z == 1));

    if (is_platform) {
        api->player_send_notice(player, "You should try to destroy the ennemy's tower... Not the platform!");
        return PLUGIN_DENY;
    }

    // Allow spade destruction anywhere (for demonstration)
    if (tool == TOOL_SPADE) {
        return PLUGIN_ALLOW;
    }

    // Prevent teams from destroying their own towers
    plugin_team_t team = api->player_get_team(server, player);

    // Team 1 tower is on the right (x > 512-220 = 292)
    // Team 0 tower is on the left (x < 220)
    if ((team.id == 1 && x > 292) || (team.id == 0 && x < 220)) {
        api->player_send_notice(player, "You should try to destroy the ennemy's tower... It is not on this side of the map!");
        return PLUGIN_DENY;
    }

    return PLUGIN_ALLOW;
}

// Block placement check
PLUGIN_EXPORT int spadesx_plugin_on_block_place(server_t* server, player_t* player, block_t* block)
{
    plugin_team_t team = api->player_get_team(server, player);
    uint32_t player_color = api->player_get_color(player);

    // Force block to be placed in team color
    if (block->color != team.color) {
        block->color = team.color;
    }

    // Update the player's tool color if it's wrong and broadcast to all clients
    if (player_color != team.color) {
        api->player_set_color_broadcast(server, player, team.color);
    }

    // Auto restock when blocks are low
    if (api->player_get_blocks(player) < 10) {
        api->player_restock(player);
    }

    return PLUGIN_ALLOW;
}

// Command handler
PLUGIN_EXPORT int spadesx_plugin_on_command(server_t* server, player_t* player, const char* command)
{
    (void) server;
    if (strcmp(command, "/restock") == 0) {
        api->player_restock(player);
        api->player_send_notice(player, "Restocked!");
        return PLUGIN_ALLOW;  // Command was handled
    }

    return PLUGIN_DENY;  // Command not handled
}

// Player connect
PLUGIN_EXPORT void spadesx_plugin_on_player_connect(server_t* server, player_t* player)
{
    (void) server;
    const char* name = api->player_get_name(player);
    api->log_info(PLUGIN_NAME, "Player %s connected", name);

    // Welcome the player
    api->player_send_notice(player, "Welcome to the Babel-style server!");
    api->player_send_notice(player, "Type /restock to refill your blocks and grenades");
    api->player_send_notice(player, "Headshots only mode enabled!");
}

// Player disconnect
PLUGIN_EXPORT void spadesx_plugin_on_player_disconnect(server_t* server, player_t* player, const char* reason)
{
    (void)server;
    const char* name = api->player_get_name(player);
    api->log_info(PLUGIN_NAME, "Player %s disconnected: %s", name, reason);
    // Leave the trail behind - don't remove blocks
}

// Player hit handler - only allow headshots
PLUGIN_EXPORT int
spadesx_plugin_on_player_hit(server_t* server, player_t* shooter, player_t* victim, uint8_t hit_type, uint8_t weapon)
{
    (void) server;
    (void) weapon;

    const char* shooter_name = api->player_get_name(shooter);
    const char* victim_name  = api->player_get_name(victim);
    const char* hit_location;

    // Get hit location name
    switch (hit_type) {
        case 0:
            hit_location = "torso";
            break;
        case 1:
            hit_location = "head";
            break;
        case 2:
            hit_location = "arms";
            break;
        case 3:
            hit_location = "legs";
            break;
        case 4:
            hit_location = "melee";
            break;
        default:
            hit_location = "unknown";
            break;
    }

    api->log_debug(PLUGIN_NAME, "%s hit %s in the %s", shooter_name, victim_name, hit_location);

    if (hit_type != 1 && hit_type != 4) { // 1=head, 4=melee
        api->player_send_notice(shooter, "Headshots only!");
        return PLUGIN_DENY;
    }

    return PLUGIN_ALLOW;
}

// Tick handler - runs 60 times per second
// Update blocks above all players - leaves a trail
PLUGIN_EXPORT void spadesx_plugin_on_tick(server_t* server)
{
    tick_counter++;

    // Debug log every 5 seconds (60 ticks/sec * 5 = 300 ticks)
    if (tick_counter % 300 == 0) {
        api->log_info(PLUGIN_NAME, "Tick handler running (tick %d)", tick_counter);
    }

    // Iterate through all possible player IDs
    for (uint8_t i = 0; i < 32; i++) {
        player_t* player = api->get_player(server, i);

        // Skip if player doesn't exist or is dead
        if (!player) {
            // Just reset tracking, don't remove blocks (leave the trail)
            player_blocks[i].has_block = 0;
            continue;
        }

        // Get player position
        vector3f_t pos = api->player_get_position(player);

        // Log first time we see this player
        if (!player_blocks[i].has_block) {
            const char* name = api->player_get_name(player);
            api->log_info(PLUGIN_NAME, "Found player %s (ID %d) at position (%.1f, %.1f, %.1f)",
                         name, i, pos.x, pos.y, pos.z);
        }

        // Calculate block position (3 meters above player's head)
        // In Ace of Spades, Z increases downward, so we subtract 3
        int32_t block_x = (int32_t)(pos.x);
        int32_t block_y = (int32_t)(pos.y);
        int32_t block_z = (int32_t)(pos.z) - 3;

        // Check if block position changed
        int position_changed = 0;
        if (player_blocks[i].has_block) {
            if (player_blocks[i].block_x != block_x ||
                player_blocks[i].block_y != block_y ||
                player_blocks[i].block_z != block_z) {
                position_changed = 1;
            }
        } else {
            position_changed = 1;
        }

        // Only place block if position changed (leave trail behind)
        if (position_changed) {
            map_t* map = api->get_map(server);
            if (api->map_is_valid_pos(map, block_x, block_y, block_z)) {
                // Use a bright color (yellow) so it's easy to see
                uint32_t color = 0xFFFFFF00; // Yellow (ARGB format)

                plugin_result_t result = api->map_set_block(server, block_x, block_y, block_z, color);


                // Update tracking
                player_blocks[i].block_x = block_x;
                player_blocks[i].block_y = block_y;
                player_blocks[i].block_z = block_z;
                player_blocks[i].has_block = 1;
            } else {
            }
        }
    }
}

// All functions are exported directly with PLUGIN_EXPORT above
