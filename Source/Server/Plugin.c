// Plugin.c - Plugin management implementation

#include "Plugin.h"
#include "Player.h"
#include "Structs/PlayerStruct.h"
#include "Structs/ServerStruct.h"
#include "Packets/Packets.h"
#include "Commands/CommandManager.h"
#include "Map.h"
#include "Util/Log.h"
#include "Util/Uthash.h"
#include "Util/Notice.h"
#include "Util/Enums.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform-specific dynamic library loading
#ifdef _WIN32
    #include <windows.h>
    #define DLOPEN(path)    LoadLibraryA(path)
    #define DLSYM(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
    #define DLCLOSE(handle) FreeLibrary((HMODULE)handle)
    #define DLERROR()       "Windows LoadLibrary error"
#else
    #include <dlfcn.h>
    #ifdef __APPLE__
        #define DLOPEN(path)    dlopen(path, RTLD_NOW | RTLD_LOCAL | RTLD_FIRST)
    #else
        #define DLOPEN(path)    dlopen(path, RTLD_NOW | RTLD_LOCAL)
    #endif
    #define DLSYM(handle, symbol) dlsym(handle, symbol)
    #define DLCLOSE(handle) dlclose(handle)
    #define DLERROR()       dlerror()
#endif

// Global plugin list
static plugin_t* g_plugins = NULL;

// Global server reference for API functions that need it
static server_t* g_server = NULL;

// Forward declarations for API implementation
static player_t* api_get_player(server_t* server, uint8_t player_id);
static const char* api_player_get_name(player_t* player);
static plugin_team_t api_player_get_team(server_t* server, player_t* player);
static uint8_t api_player_get_tool(player_t* player);
static uint8_t api_player_get_blocks(player_t* player);
static uint8_t api_player_get_grenades(player_t* player);
static uint32_t api_player_get_color(player_t* player);
static void api_player_set_color(player_t* player, uint32_t color);
static void api_player_restock(player_t* player);
static void api_player_send_notice(player_t* player, const char* message);
static void api_player_kill(player_t* player);
static void api_player_set_hp(player_t* player, uint8_t hp);
static uint8_t api_player_get_hp(player_t* player);
static vector3f_t api_player_get_position(player_t* player);
static void api_player_set_position(player_t* player, vector3f_t position);
static map_t* api_get_map(server_t* server);
static uint32_t api_map_get_block(map_t* map, int32_t x, int32_t y, int32_t z);
static void api_map_set_block(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color);
static void api_map_remove_block(server_t* server, int32_t x, int32_t y, int32_t z);
static int32_t api_map_find_top_block(map_t* map, int32_t x, int32_t y);
static int api_map_is_valid_pos(map_t* map, int32_t x, int32_t y, int32_t z);
static void api_init_add_block(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color);
static void api_init_set_intel_position(server_t* server, uint8_t team_id, int32_t x, int32_t y, int32_t z);
static void api_broadcast_message(server_t* server, const char* message);
static int api_register_command(server_t* server, const char* command_name, const char* description,
                                 void (*handler)(server_t*, player_t*, const char*), uint32_t required_permissions);

// Global plugin API instance
static plugin_api_t g_plugin_api = {
    .get_player = api_get_player,
    .player_get_name = api_player_get_name,
    .player_get_team = api_player_get_team,
    .player_get_tool = api_player_get_tool,
    .player_get_blocks = api_player_get_blocks,
    .player_get_grenades = api_player_get_grenades,
    .player_get_color = api_player_get_color,
    .player_set_color = api_player_set_color,
    .player_restock = api_player_restock,
    .player_send_notice = api_player_send_notice,
    .player_kill = api_player_kill,
    .player_set_hp = api_player_set_hp,
    .player_get_hp = api_player_get_hp,
    .player_get_position = api_player_get_position,
    .player_set_position = api_player_set_position,
    .get_map = api_get_map,
    .map_get_block = api_map_get_block,
    .map_set_block = api_map_set_block,
    .map_remove_block = api_map_remove_block,
    .map_find_top_block = api_map_find_top_block,
    .map_is_valid_pos = api_map_is_valid_pos,
    .init_add_block = api_init_add_block,
    .init_set_intel_position = api_init_set_intel_position,
    .broadcast_message = api_broadcast_message,
    .register_command = api_register_command
};

// ============================================================================
// PLUGIN SYSTEM MANAGEMENT
// ============================================================================

void plugin_system_init(server_t* server)
{
    LOG_INFO("Initializing plugin system");

    // Store server reference for API functions
    g_server = server;

    // TODO: Read plugin list from config.toml
    // For now, try to load the example plugin if it exists

#ifdef __APPLE__
    const char* plugin_path = "plugins/example_gamemode.dylib";
#elif defined(_WIN32)
    const char* plugin_path = "plugins/example_gamemode.dll";
#else
    const char* plugin_path = "plugins/example_gamemode.so";
#endif

    // Try to load the example plugin
    if (plugin_load(server, plugin_path) == 0) {
        LOG_INFO("Successfully loaded plugin from %s", plugin_path);
    } else {
        LOG_WARNING("Could not load plugin from %s (this is ok if you don't have plugins)", plugin_path);
    }
}

void plugin_system_shutdown(server_t* server)
{
    LOG_INFO("Shutting down plugin system");

    plugin_t* plugin = g_plugins;
    plugin_t* next;

    while (plugin != NULL) {
        next = plugin->next;
        plugin_unload(server, plugin);
        plugin = next;
    }

    g_plugins = NULL;
}

int plugin_load(server_t* server, const char* path)
{
    LOG_INFO("Loading plugin: %s", path);

    // Open the shared library
    void* handle = DLOPEN(path);
    if (!handle) {
        LOG_ERROR("Failed to load plugin %s: %s", path, DLERROR());
        return -1;
    }

    // Load plugin info
    plugin_info_t* info = (plugin_info_t*)DLSYM(handle, "spadesx_plugin_info");
    if (!info) {
        LOG_ERROR("Plugin %s missing spadesx_plugin_info export", path);
        DLCLOSE(handle);
        return -1;
    }

    // Check API version
    if (info->api_version != SPADESX_PLUGIN_API_VERSION) {
        LOG_ERROR("Plugin %s has incompatible API version %u (expected %u)",
                  path, info->api_version, SPADESX_PLUGIN_API_VERSION);
        DLCLOSE(handle);
        return -1;
    }

    LOG_INFO("  Name: %s v%s by %s", info->name, info->version, info->author);
    LOG_INFO("  Description: %s", info->description);

    // Create plugin structure
    plugin_t* plugin = (plugin_t*)calloc(1, sizeof(plugin_t));
    if (!plugin) {
        LOG_ERROR("Failed to allocate memory for plugin");
        DLCLOSE(handle);
        return -1;
    }

    plugin->handle = handle;
    strncpy(plugin->name, info->name, sizeof(plugin->name) - 1);
    strncpy(plugin->version, info->version, sizeof(plugin->version) - 1);
    strncpy(plugin->path, path, sizeof(plugin->path) - 1);

    // Load required functions
    LOG_INFO("  Loading spadesx_plugin_init...");
    void* init_sym = DLSYM(handle, "spadesx_plugin_init");
    if (!init_sym) {
        const char* err = DLERROR();
        LOG_ERROR("Plugin %s missing spadesx_plugin_init export: %s", path, err ? err : "unknown error");
        free(plugin);
        DLCLOSE(handle);
        return -1;
    }
    LOG_INFO("  Found spadesx_plugin_init symbol at %p", init_sym);
    plugin->init = (plugin_init_fn)init_sym;
    LOG_INFO("  Cast to function pointer: %p", (void*)plugin->init);

    plugin->shutdown = (plugin_shutdown_fn)DLSYM(handle, "spadesx_plugin_shutdown");
    LOG_INFO("  Found spadesx_plugin_shutdown at %p", (void*)plugin->shutdown);

    // Load optional event handlers
    LOG_INFO("  Loading event handlers...");
    plugin->on_server_init = (plugin_on_server_init_fn)DLSYM(handle, "spadesx_plugin_on_server_init");
    plugin->on_server_shutdown = (plugin_on_server_shutdown_fn)DLSYM(handle, "spadesx_plugin_on_server_shutdown");
    plugin->on_block_destroy = (plugin_on_block_destroy_fn)DLSYM(handle, "spadesx_plugin_on_block_destroy");
    plugin->on_block_place = (plugin_on_block_place_fn)DLSYM(handle, "spadesx_plugin_on_block_place");
    plugin->on_command = (plugin_on_command_fn)DLSYM(handle, "spadesx_plugin_on_command");
    plugin->on_player_connect = (plugin_on_player_connect_fn)DLSYM(handle, "spadesx_plugin_on_player_connect");
    plugin->on_player_disconnect = (plugin_on_player_disconnect_fn)DLSYM(handle, "spadesx_plugin_on_player_disconnect");
    plugin->on_grenade_explode = (plugin_on_grenade_explode_fn)DLSYM(handle, "spadesx_plugin_on_grenade_explode");
    plugin->on_tick = (plugin_on_tick_fn)DLSYM(handle, "spadesx_plugin_on_tick");
    plugin->on_player_hit = (plugin_on_player_hit_fn)DLSYM(handle, "spadesx_plugin_on_player_hit");
    LOG_INFO("  Event handlers loaded");

    // Call plugin init
    LOG_INFO("  Calling plugin init function...");
    LOG_INFO("  Function pointer: %p", (void*)plugin->init);
    LOG_INFO("  Server: %p, API: %p", (void*)server, (void*)&g_plugin_api);

    int init_result = plugin->init(server, &g_plugin_api);

    LOG_INFO("  Plugin init returned: %d", init_result);

    if (init_result != 0) {
        LOG_ERROR("Plugin %s initialization failed", info->name);
        free(plugin);
        DLCLOSE(handle);
        return -1;
    }

    // Add to plugin list
    plugin->next = g_plugins;
    g_plugins = plugin;

    LOG_INFO("Plugin %s loaded successfully", info->name);
    return 0;
}

void plugin_unload(server_t* server, plugin_t* plugin)
{
    if (!plugin) {
        return;
    }

    LOG_INFO("Unloading plugin: %s", plugin->name);

    // Call shutdown if available
    if (plugin->shutdown) {
        plugin->shutdown(server);
    }

    // Close the library
    if (plugin->handle) {
        DLCLOSE(plugin->handle);
    }

    // Remove from list
    plugin_t** pp = &g_plugins;
    while (*pp) {
        if (*pp == plugin) {
            *pp = plugin->next;
            break;
        }
        pp = &(*pp)->next;
    }

    free(plugin);
}

const plugin_api_t* plugin_get_api(void)
{
    return &g_plugin_api;
}

// ============================================================================
// EVENT DISPATCHERS
// ============================================================================

void plugin_dispatch_server_init(server_t* server)
{
    LOG_INFO("Dispatching server_init event to plugins...");
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_server_init) {
            LOG_INFO("Calling on_server_init for plugin: %s", p->name);
            p->on_server_init(server, &g_plugin_api);
            LOG_INFO("Finished on_server_init for plugin: %s", p->name);
        }
    }
    LOG_INFO("Finished dispatching server_init event");
}

void plugin_dispatch_server_shutdown(server_t* server)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_server_shutdown) {
            p->on_server_shutdown(server);
        }
    }
}

int plugin_dispatch_block_destroy(server_t* server, player_t* player, uint8_t tool, block_t* block)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_block_destroy) {
            if (p->on_block_destroy(server, player, tool, block) == PLUGIN_DENY) {
                return PLUGIN_DENY;
            }
        }
    }
    return PLUGIN_ALLOW;
}

int plugin_dispatch_block_place(server_t* server, player_t* player, block_t* block)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_block_place) {
            if (p->on_block_place(server, player, block) == PLUGIN_DENY) {
                return PLUGIN_DENY;
            }
        }
    }
    return PLUGIN_ALLOW;
}

int plugin_dispatch_command(server_t* server, player_t* player, const char* command)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_command) {
            if (p->on_command(server, player, command) == PLUGIN_ALLOW) {
                return PLUGIN_ALLOW;  // Command was handled
            }
        }
    }
    return PLUGIN_DENY;  // No plugin handled the command
}

void plugin_dispatch_player_connect(server_t* server, player_t* player)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_player_connect) {
            p->on_player_connect(server, player);
        }
    }
}

void plugin_dispatch_player_disconnect(server_t* server, player_t* player, const char* reason)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_player_disconnect) {
            p->on_player_disconnect(server, player, reason);
        }
    }
}

void plugin_dispatch_grenade_explode(server_t* server, player_t* player, vector3f_t position)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_grenade_explode) {
            p->on_grenade_explode(server, player, position);
        }
    }
}

void plugin_dispatch_tick(server_t* server)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_tick) {
            p->on_tick(server);
        }
    }
}

int plugin_dispatch_player_hit(server_t* server, player_t* shooter, player_t* victim, uint8_t hit_type, uint8_t weapon)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_player_hit) {
            if (p->on_player_hit(server, shooter, victim, hit_type, weapon) == PLUGIN_DENY) {
                return PLUGIN_DENY;
            }
        }
    }
    return PLUGIN_ALLOW;
}

// ============================================================================
// PLUGIN API IMPLEMENTATION
// ============================================================================

static player_t* api_get_player(server_t* server, uint8_t player_id)
{
    player_t* player;
    HASH_FIND(hh, server->players, &player_id, sizeof(uint8_t), player);
    return player;
}

static const char* api_player_get_name(player_t* player)
{
    return player ? player->name : "";
}

static plugin_team_t api_player_get_team(server_t* server, player_t* player)
{
    plugin_team_t team = {0};
    if (server && player && player->team < 2) {
        team.id = player->team;
        // Copy team name and color from server protocol
        strncpy(team.name, server->protocol.name_team[player->team], sizeof(team.name) - 1);
        team.color = server->protocol.color_team[player->team].raw;
    }
    return team;
}

static uint8_t api_player_get_tool(player_t* player)
{
    return player ? player->item : TOOL_SPADE;
}

static uint8_t api_player_get_blocks(player_t* player)
{
    return player ? player->blocks : 0;
}

static uint8_t api_player_get_grenades(player_t* player)
{
    return player ? player->grenades : 0;
}

static uint32_t api_player_get_color(player_t* player)
{
    return player ? player->tool_color.raw : 0;
}

static void api_player_set_color(player_t* player, uint32_t color)
{
    if (player) {
        player->tool_color.raw = color;
    }
}

static void api_player_restock(player_t* player)
{
    if (player && g_server) {
        player->blocks = 50;
        player->grenades = 3;
        send_restock(g_server, player);
    }
}

static void api_player_send_notice(player_t* player, const char* message)
{
    if (player && message) {
        send_server_notice(player, 0, "%s", message);
    }
}

static void api_player_kill(player_t* player)
{
    if (player && player->alive) {
        player->hp = 0;
        player->alive = 0;
        // The actual kill packet sending should be done through proper channels
        // This is a simplified version
    }
}

static void api_player_set_hp(player_t* player, uint8_t hp)
{
    if (player) {
        player->hp = hp > 100 ? 100 : hp;
        if (hp == 0) {
            player->alive = 0;
        }
    }
}

static uint8_t api_player_get_hp(player_t* player)
{
    return player ? player->hp : 0;
}

static vector3f_t api_player_get_position(player_t* player)
{
    vector3f_t pos = {0, 0, 0};
    if (player) {
        pos.x = player->movement.position.x;
        pos.y = player->movement.position.y;
        pos.z = player->movement.position.z;
    }
    return pos;
}

static void api_player_set_position(player_t* player, vector3f_t position)
{
    if (player) {
        player->movement.position.x = position.x;
        player->movement.position.y = position.y;
        player->movement.position.z = position.z;
    }
}

static map_t* api_get_map(server_t* server)
{
    return &server->s_map;
}

static uint32_t api_map_get_block(map_t* map, int32_t x, int32_t y, int32_t z)
{
    if (map) {
        return mapvxl_get_color(&map->map, x, y, z);
    }
    return 0;
}

static void api_map_set_block(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color)
{
    if (server) {
        mapvxl_set_color(&server->s_map.map, x, y, z, color);
        // TODO: Send block update packet to all players
    }
}

static void api_map_remove_block(server_t* server, int32_t x, int32_t y, int32_t z)
{
    if (server) {
        mapvxl_set_air(&server->s_map.map, x, y, z);
        // TODO: Send block update packet to all players
    }
}

static int32_t api_map_find_top_block(map_t* map, int32_t x, int32_t y)
{
    if (map) {
        return mapvxl_find_top_block(&map->map, x, y);
    }
    return -1;
}

static int api_map_is_valid_pos(map_t* map, int32_t x, int32_t y, int32_t z)
{
    if (map) {
        return (x >= 0 && x < 512 && y >= 0 && y < 512 && z >= 0 && z < 64);
    }
    return 0;
}

static void api_init_add_block(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color)
{
    // During initialization, we can directly modify the map without sending packets
    mapvxl_set_color(&server->s_map.map, x, y, z, color);
}

static void api_init_set_intel_position(server_t* server, uint8_t team_id, int32_t x, int32_t y, int32_t z)
{
    if (team_id < 2) {
        server->protocol.gamemode.intel[team_id].x = x;
        server->protocol.gamemode.intel[team_id].y = y;
        server->protocol.gamemode.intel[team_id].z = z;
    }
}

static void api_broadcast_message(server_t* server, const char* message)
{
    if (server && message) {
        broadcast_server_notice(server, 0, "%s", message);
    }
}

static int api_register_command(server_t* server, const char* command_name, const char* description,
                                 void (*handler)(server_t*, player_t*, const char*), uint32_t required_permissions)
{
    // TODO: Implement custom command registration
    // For now, just log that a plugin wants to register a command
    (void)server;
    (void)description;
    (void)required_permissions;

    if (!command_name || !handler) {
        return 0;
    }

    LOG_INFO("Plugin requested to register command: %s", command_name);
    return 1;  // Pretend it worked
}
