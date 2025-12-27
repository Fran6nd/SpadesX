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
#include <stdarg.h>
#include "Util/Alloc.h"
#include "Structs/CommandStruct.h"

// Platform-specific dynamic library loading
#ifdef _WIN32
    #include <windows.h>
    #define DLOPEN(path)    LoadLibraryA(path)
    #define DLSYM(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
    #define DLCLOSE(handle) FreeLibrary((HMODULE)handle)
    #define DLERROR()       "Windows LoadLibrary error"
#else
    #include <dlfcn.h>
    #include <dirent.h>
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

// Plugin command tracking structure
typedef struct plugin_command {
    char command_name[30];
    void (*handler)(server_t*, player_t*, const char*);
    struct plugin_command* next;
} plugin_command_t;

static plugin_command_t* g_plugin_commands = NULL;

// Forward declarations for API implementation
static player_t* api_get_player(server_t* server, uint8_t player_id);
static const char* api_player_get_name(player_t* player);
static plugin_team_t api_player_get_team(server_t* server, player_t* player);
static uint8_t api_player_get_tool(player_t* player);
static uint8_t api_player_get_blocks(player_t* player);
static uint8_t api_player_get_grenades(player_t* player);
static uint32_t api_player_get_color(player_t* player);
static plugin_result_t api_player_set_color(player_t* player, uint32_t color);
static plugin_result_t api_player_set_color_broadcast(server_t* server, player_t* player, uint32_t color);
static plugin_result_t api_player_restock(player_t* player);
static plugin_result_t api_player_send_notice(player_t* player, const char* message);
static plugin_result_t api_player_kill(player_t* player);
static plugin_result_t api_player_set_hp(player_t* player, uint8_t hp);
static uint8_t api_player_get_hp(player_t* player);
static vector3f_t api_player_get_position(player_t* player);
static plugin_result_t api_player_set_position(player_t* player, vector3f_t position);
static map_t* api_get_map(server_t* server);
static uint32_t api_map_get_block(map_t* map, int32_t x, int32_t y, int32_t z);
static plugin_result_t api_map_set_block(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color);
static plugin_result_t api_map_remove_block(server_t* server, int32_t x, int32_t y, int32_t z);
static int32_t api_map_find_top_block(map_t* map, int32_t x, int32_t y);
static int api_map_is_valid_pos(map_t* map, int32_t x, int32_t y, int32_t z);
static plugin_result_t api_init_add_block(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color);
static plugin_result_t api_init_set_intel_position(server_t* server, uint8_t team_id, int32_t x, int32_t y, int32_t z);
static plugin_result_t api_broadcast_message(server_t* server, const char* message);
static plugin_result_t api_register_command(server_t* server, const char* command_name, const char* description,
                                 void (*handler)(server_t*, player_t*, const char*), uint32_t required_permissions);
static void api_log_message(const char* plugin_name, plugin_log_level_t level, const char* format, ...);
static void api_log_debug(const char* plugin_name, const char* format, ...);
static void api_log_info(const char* plugin_name, const char* format, ...);
static void api_log_warning(const char* plugin_name, const char* format, ...);
static void api_log_error(const char* plugin_name, const char* format, ...);

// ============================================================================
// ERROR HANDLING
// ============================================================================

const char* plugin_result_to_string(plugin_result_t result)
{
    switch (result) {
        case PLUGIN_OK: return "Success";
        case PLUGIN_ALLOW: return "Allow";
        case PLUGIN_DENY: return "Deny";
        case PLUGIN_ERROR: return "Generic error";
        case PLUGIN_ERROR_INVALID_PARAM: return "Invalid parameter";
        case PLUGIN_ERROR_NULL_POINTER: return "NULL pointer";
        case PLUGIN_ERROR_OUT_OF_RANGE: return "Value out of range";
        case PLUGIN_ERROR_NOT_FOUND: return "Not found";
        case PLUGIN_ERROR_PERMISSION_DENIED: return "Permission denied";
        case PLUGIN_ERROR_INVALID_STATE: return "Invalid state";
        case PLUGIN_ERROR_PLAYER_NOT_FOUND: return "Player not found";
        case PLUGIN_ERROR_PLAYER_DEAD: return "Player is dead";
        case PLUGIN_ERROR_PLAYER_DISCONNECTED: return "Player disconnected";
        case PLUGIN_ERROR_INVALID_TEAM: return "Invalid team ID";
        case PLUGIN_ERROR_INVALID_HP: return "Invalid HP value";
        case PLUGIN_ERROR_MAP_OUT_OF_BOUNDS: return "Map coordinates out of bounds";
        case PLUGIN_ERROR_MAP_INVALID_COLOR: return "Invalid color value";
        case PLUGIN_ERROR_MAP_NO_BLOCK: return "No block at position";
        case PLUGIN_ERROR_CMD_ALREADY_REGISTERED: return "Command already registered";
        case PLUGIN_ERROR_CMD_INVALID_NAME: return "Invalid command name";
        case PLUGIN_ERROR_CMD_TOO_MANY: return "Too many commands registered";
        default: return "Unknown error code";
    }
}

// ============================================================================
// PLUGIN API INSTANCE
// ============================================================================

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
    .player_set_color_broadcast = api_player_set_color_broadcast,
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
    .register_command = api_register_command,
    .log_message = api_log_message,
    .log_debug = api_log_debug,
    .log_info = api_log_info,
    .log_warning = api_log_warning,
    .log_error = api_log_error
};

// ============================================================================
// PLUGIN SYSTEM MANAGEMENT
// ============================================================================

// Helper function to check if a filename has a plugin extension
static int is_plugin_file(const char* filename)
{
    if (!filename) return 0;

    size_t len = strlen(filename);

#ifdef __APPLE__
    return (len > 6 && strcmp(filename + len - 6, ".dylib") == 0);
#elif defined(_WIN32)
    return (len > 4 && strcmp(filename + len - 4, ".dll") == 0);
#else
    return (len > 3 && strcmp(filename + len - 3, ".so") == 0);
#endif
}

void plugin_system_init(server_t* server)
{
    LOG_INFO("Initializing plugin system");

    // Store server reference for API functions
    g_server = server;

    const char* plugin_dir = "plugins";
    int plugins_loaded = 0;

#ifdef _WIN32
    // Windows directory scanning
    WIN32_FIND_DATAA find_data;
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*.dll", plugin_dir);

    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char plugin_path[512];
                snprintf(plugin_path, sizeof(plugin_path), "%s\\%s", plugin_dir, find_data.cFileName);

                LOG_INFO("Found plugin: %s", find_data.cFileName);
                if (plugin_load(server, plugin_path) == 0) {
                    plugins_loaded++;
                    LOG_INFO("Successfully loaded plugin from %s", plugin_path);
                } else {
                    LOG_WARNING("Failed to load plugin from %s", plugin_path);
                }
            }
        } while (FindNextFileA(hFind, &find_data) != 0);
        FindClose(hFind);
    } else {
        LOG_WARNING("Could not open plugins directory: %s", plugin_dir);
    }
#else
    // Unix/Linux/macOS directory scanning
    DIR* dir = opendir(plugin_dir);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            // Skip . and .. directories
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // Check if file has plugin extension
            if (is_plugin_file(entry->d_name)) {
                char plugin_path[512];
                snprintf(plugin_path, sizeof(plugin_path), "%s/%s", plugin_dir, entry->d_name);

                LOG_INFO("Found plugin: %s", entry->d_name);
                if (plugin_load(server, plugin_path) == 0) {
                    plugins_loaded++;
                    LOG_INFO("Successfully loaded plugin from %s", plugin_path);
                } else {
                    LOG_WARNING("Failed to load plugin from %s", plugin_path);
                }
            }
        }
        closedir(dir);
    } else {
        LOG_WARNING("Could not open plugins directory: %s (this is ok if you don't have plugins)", plugin_dir);
    }
#endif

    if (plugins_loaded == 0) {
        LOG_INFO("No plugins loaded");
    } else {
        LOG_INFO("Successfully loaded %d plugin(s)", plugins_loaded);
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
    plugin->on_color_change = (plugin_on_color_change_fn)DLSYM(handle, "spadesx_plugin_on_color_change");
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

int plugin_dispatch_color_change(server_t* server, player_t* player, uint32_t* new_color)
{
    for (plugin_t* p = g_plugins; p != NULL; p = p->next) {
        if (p->on_color_change) {
            if (p->on_color_change(server, player, new_color) == PLUGIN_DENY) {
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

static plugin_result_t api_player_set_color(player_t* player, uint32_t color)
{
    if (!player) {
        return PLUGIN_ERROR_NULL_POINTER;
    }
    player->tool_color.raw = color;
    return PLUGIN_OK;
}

static plugin_result_t api_player_set_color_broadcast(server_t* server, player_t* player, uint32_t color)
{
    if (!server) {
        return PLUGIN_ERROR_NULL_POINTER;
    }
    if (!player) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    player->tool_color.raw = color;
    // Send to the player first
    send_set_color_to_player(server, player, player, player->tool_color);
    // Then broadcast to all other clients
    send_set_color(server, player, player->tool_color);

    return PLUGIN_OK;
}

static plugin_result_t api_player_restock(player_t* player)
{
    if (!player) {
        return PLUGIN_ERROR_NULL_POINTER;
    }
    if (!g_server) {
        return PLUGIN_ERROR_INVALID_STATE;
    }

    player->blocks = 50;
    player->grenades = 3;
    send_restock(g_server, player);

    return PLUGIN_OK;
}

static plugin_result_t api_player_send_notice(player_t* player, const char* message)
{
    if (!player) {
        return PLUGIN_ERROR_NULL_POINTER;
    }
    if (!message) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    send_server_notice(player, 0, "%s", message);
    return PLUGIN_OK;
}

static plugin_result_t api_player_kill(player_t* player)
{
    if (!player) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    player->hp = 0;
    player->alive = 0;
    // The actual kill packet sending should be done through proper channels
    // This is a simplified version

    return PLUGIN_OK;
}

static plugin_result_t api_player_set_hp(player_t* player, uint8_t hp)
{
    if (!player) {
        return PLUGIN_ERROR_NULL_POINTER;
    }
    if (hp > 100) {
        return PLUGIN_ERROR_INVALID_HP;
    }

    player->hp = hp;
    if (hp == 0) {
        player->alive = 0;
    }

    return PLUGIN_OK;
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

static plugin_result_t api_player_set_position(player_t* player, vector3f_t position)
{
    if (!player) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    // Validate position is within reasonable bounds
    if (position.x < 0 || position.x >= 512 ||
        position.y < 0 || position.y >= 512 ||
        position.z < 0 || position.z >= 64) {
        return PLUGIN_ERROR_OUT_OF_RANGE;
    }

    player->movement.position.x = position.x;
    player->movement.position.y = position.y;
    player->movement.position.z = position.z;

    return PLUGIN_OK;
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

static plugin_result_t api_map_set_block(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color)
{
    if (!server) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    // Validate coordinates
    if (x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0 || z >= 64) {
        return PLUGIN_ERROR_MAP_OUT_OF_BOUNDS;
    }

    mapvxl_set_color(&server->s_map.map, x, y, z, color);

    // Send block update to all players
    // Use player ID 33 (invalid/server ID) so clients won't decrement any player's block count
    // The Ace of Spades protocol only supports 32 players (IDs 0-31)
    if (server->protocol.num_players > 0) {
        ENetPacket* packet = enet_packet_create(NULL, 15, ENET_PACKET_FLAG_RELIABLE);
        stream_t stream = {packet->data, packet->dataLength, 0};
        stream_write_u8(&stream, PACKET_TYPE_BLOCK_ACTION);
        stream_write_u8(&stream, 33);  // Server/invalid player ID
        stream_write_u8(&stream, BLOCKACTION_BUILD);
        stream_write_u32(&stream, x);
        stream_write_u32(&stream, y);
        stream_write_u32(&stream, z);

        uint8_t sent = 0;
        player_t* p;
        player_t* tmp;
        HASH_ITER(hh, server->players, p, tmp) {
            if (is_past_state_data(p)) {
                if (enet_peer_send(p->peer, 0, packet) == 0) {
                    sent = 1;
                }
            } else if (p->state == STATE_STARTING_MAP || p->state == STATE_LOADING_CHUNKS) {
                // Store in block buffer for players still loading
                block_node_t* node = (block_node_t*) spadesx_malloc(sizeof(*node));
                node->position.x = x;
                node->position.y = y;
                node->position.z = z;
                node->color.raw = color;
                node->type = BLOCKACTION_BUILD;
                node->sender_id = 33;  // Server ID
                LL_APPEND(p->blockBuffer, node);
            }
        }
        if (sent == 0) {
            enet_packet_destroy(packet);
        }
    }

    return PLUGIN_OK;
}

static plugin_result_t api_map_remove_block(server_t* server, int32_t x, int32_t y, int32_t z)
{
    if (!server) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    // Validate coordinates
    if (x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0 || z >= 64) {
        return PLUGIN_ERROR_MAP_OUT_OF_BOUNDS;
    }

    mapvxl_set_air(&server->s_map.map, x, y, z);

    // Send block destroy action to all players
    // Use player ID 33 (invalid/server ID) so clients won't affect any player's block count
    if (server->protocol.num_players > 0) {
        ENetPacket* packet = enet_packet_create(NULL, 15, ENET_PACKET_FLAG_RELIABLE);
        stream_t stream = {packet->data, packet->dataLength, 0};
        stream_write_u8(&stream, PACKET_TYPE_BLOCK_ACTION);
        stream_write_u8(&stream, 33);  // Server/invalid player ID
        stream_write_u8(&stream, BLOCKACTION_DESTROY_ONE);
        stream_write_u32(&stream, x);
        stream_write_u32(&stream, y);
        stream_write_u32(&stream, z);

        uint8_t sent = 0;
        player_t* p;
        player_t* tmp;
        HASH_ITER(hh, server->players, p, tmp) {
            if (is_past_state_data(p)) {
                if (enet_peer_send(p->peer, 0, packet) == 0) {
                    sent = 1;
                }
            } else if (p->state == STATE_STARTING_MAP || p->state == STATE_LOADING_CHUNKS) {
                // Store in block buffer for players still loading
                block_node_t* node = (block_node_t*) spadesx_malloc(sizeof(*node));
                node->position.x = x;
                node->position.y = y;
                node->position.z = z;
                node->color.raw = 0;  // Color not needed for destroy
                node->type = BLOCKACTION_DESTROY_ONE;
                node->sender_id = 33;  // Server ID
                LL_APPEND(p->blockBuffer, node);
            }
        }
        if (sent == 0) {
            enet_packet_destroy(packet);
        }
    }

    return PLUGIN_OK;
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

static plugin_result_t api_init_add_block(server_t* server, int32_t x, int32_t y, int32_t z, uint32_t color)
{
    if (!server) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    // Validate coordinates
    if (x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0 || z >= 64) {
        return PLUGIN_ERROR_MAP_OUT_OF_BOUNDS;
    }

    // During initialization, we can directly modify the map without sending packets
    mapvxl_set_color(&server->s_map.map, x, y, z, color);
    return PLUGIN_OK;
}

static plugin_result_t api_init_set_intel_position(server_t* server, uint8_t team_id, int32_t x, int32_t y, int32_t z)
{
    if (!server) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    if (team_id >= 2) {
        return PLUGIN_ERROR_INVALID_TEAM;
    }

    // Validate coordinates are reasonable
    if (x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0 || z >= 64) {
        return PLUGIN_ERROR_MAP_OUT_OF_BOUNDS;
    }

    server->protocol.gamemode.intel[team_id].x = x;
    server->protocol.gamemode.intel[team_id].y = y;
    server->protocol.gamemode.intel[team_id].z = z;

    return PLUGIN_OK;
}

static plugin_result_t api_broadcast_message(server_t* server, const char* message)
{
    if (!server) {
        return PLUGIN_ERROR_NULL_POINTER;
    }
    if (!message) {
        return PLUGIN_ERROR_NULL_POINTER;
    }

    broadcast_server_notice(server, 0, "%s", message);
    return PLUGIN_OK;
}

// Wrapper function to convert plugin command handler to server command handler
static void plugin_command_wrapper(void* p_server, command_args_t arguments)
{
    server_t* server = (server_t*)p_server;

    // Find the plugin command handler
    plugin_command_t* cmd;
    LL_FOREACH(g_plugin_commands, cmd) {
        if (strcmp(cmd->command_name, arguments.argv[0]) == 0) {
            // Concatenate all arguments into a single string
            char args_str[1024] = {0};
            for (uint32_t i = 1; i < arguments.argc; i++) {
                if (i > 1) {
                    strcat(args_str, " ");
                }
                strncat(args_str, arguments.argv[i], sizeof(args_str) - strlen(args_str) - 1);
            }

            // Call the plugin handler
            cmd->handler(server, arguments.player, args_str);
            return;
        }
    }
}

static plugin_result_t api_register_command(server_t* server, const char* command_name, const char* description,
                                 void (*handler)(server_t*, player_t*, const char*), uint32_t required_permissions)
{
    if (!server) {
        return PLUGIN_ERROR_NULL_POINTER;
    }
    if (!command_name || !handler) {
        return PLUGIN_ERROR_CMD_INVALID_NAME;
    }

    // Check if command already exists
    command_t* existing_cmd = NULL;
    HASH_FIND_STR(server->cmds_map, command_name, existing_cmd);
    if (existing_cmd) {
        LOG_WARNING("Plugin tried to register already existing command: %s", command_name);
        return PLUGIN_ERROR_CMD_ALREADY_REGISTERED;
    }

    // Track the plugin command handler
    plugin_command_t* plugin_cmd = spadesx_malloc(sizeof(plugin_command_t));
    strncpy(plugin_cmd->command_name, command_name, sizeof(plugin_cmd->command_name) - 1);
    plugin_cmd->handler = handler;
    LL_APPEND(g_plugin_commands, plugin_cmd);

    // Register the command in the server's command system
    command_t* cmd = spadesx_malloc(sizeof(command_t));
    cmd->execute = plugin_command_wrapper;
    cmd->parse_args = 1;  // Parse arguments
    cmd->permissions = required_permissions;

    if (description) {
        strncpy(cmd->description, description, sizeof(cmd->description) - 1);
    } else {
        snprintf(cmd->description, sizeof(cmd->description), "Plugin command: %s", command_name);
    }

    strncpy(cmd->id, command_name, sizeof(cmd->id) - 1);

    HASH_ADD_STR(server->cmds_map, id, cmd);
    LL_APPEND(server->cmds_list, cmd);

    LOG_INFO("Plugin registered command: %s", command_name);
    return PLUGIN_OK;
}

// ============================================================================
// LOGGING API IMPLEMENTATION
// ============================================================================

#include <stdarg.h>

static void api_log_message(const char* plugin_name, plugin_log_level_t level, const char* format, ...)
{
    if (!plugin_name || !format) {
        return;
    }

    va_list args;
    va_start(args, format);

    // Create a buffer for the formatted message
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);

    va_end(args);

    // Log with appropriate level and plugin name prefix
    switch (level) {
        case PLUGIN_LOG_DEBUG:
            LOG_DEBUG("[Plugin: %s] %s", plugin_name, message);
            break;
        case PLUGIN_LOG_INFO:
            LOG_INFO("[Plugin: %s] %s", plugin_name, message);
            break;
        case PLUGIN_LOG_WARNING:
            LOG_WARNING("[Plugin: %s] %s", plugin_name, message);
            break;
        case PLUGIN_LOG_ERROR:
        case PLUGIN_LOG_FATAL:
            LOG_ERROR("[Plugin: %s] %s", plugin_name, message);
            break;
        default:
            LOG_INFO("[Plugin: %s] %s", plugin_name, message);
            break;
    }
}

static void api_log_debug(const char* plugin_name, const char* format, ...)
{
    if (!plugin_name || !format) {
        return;
    }

    va_list args;
    va_start(args, format);

    char message[1024];
    vsnprintf(message, sizeof(message), format, args);

    va_end(args);

    LOG_DEBUG("[Plugin: %s] %s", plugin_name, message);
}

static void api_log_info(const char* plugin_name, const char* format, ...)
{
    if (!plugin_name || !format) {
        return;
    }

    va_list args;
    va_start(args, format);

    char message[1024];
    vsnprintf(message, sizeof(message), format, args);

    va_end(args);

    LOG_INFO("[Plugin: %s] %s", plugin_name, message);
}

static void api_log_warning(const char* plugin_name, const char* format, ...)
{
    if (!plugin_name || !format) {
        return;
    }

    va_list args;
    va_start(args, format);

    char message[1024];
    vsnprintf(message, sizeof(message), format, args);

    va_end(args);

    LOG_WARNING("[Plugin: %s] %s", plugin_name, message);
}

static void api_log_error(const char* plugin_name, const char* format, ...)
{
    if (!plugin_name || !format) {
        return;
    }

    va_list args;
    va_start(args, format);

    char message[1024];
    vsnprintf(message, sizeof(message), format, args);

    va_end(args);

    LOG_ERROR("[Plugin: %s] %s", plugin_name, message);
}
