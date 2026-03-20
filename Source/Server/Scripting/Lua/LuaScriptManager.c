// LuaScriptManager.c - Lua 5.4 scripting backend

#include <Server/Scripting/Lua/LuaScriptManager.h>
#include <Server/Scripting/Lua/LuaBindings.h>
#include <Server/Scripting/Lua/LuaTypes.h>
#include <Server/Structs/ServerStruct.h>
#include <Server/Structs/PlayerStruct.h>
#include <Server/Structs/CommandStruct.h>
#include <Util/Log.h>
#include <Util/Uthash.h>
#include <Util/Utlist.h>
#include <Util/Alloc.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <string.h>
#include <stdlib.h>

// ============================================================================
// Module state
// ============================================================================

static lua_State* g_server_lua = NULL; // Persistent server-wide state
static lua_State* g_map_lua    = NULL; // Per-map state, replaced on each rotation
static server_t*  g_server     = NULL; // Current server reference

// Key used to store the server pointer in the Lua registry.
// Using the address of this variable as a unique lightuserdata key.
static const char g_server_key = 's';

// Tracks a command registered from a Lua script so it can be cleaned up
// when the owning Lua state is closed.
typedef struct lua_cmd_entry {
    char       name[30];
    lua_State* L;
    int        func_ref;             // LUA_REGISTRYINDEX reference
    struct lua_cmd_entry* next;
} lua_cmd_entry_t;

static lua_cmd_entry_t* g_server_cmds = NULL; // Lifetime: server startup → shutdown
static lua_cmd_entry_t* g_map_cmds    = NULL; // Lifetime: map load → map unload

// ============================================================================
// Hook registry — the 'on' decorator proxy injected into g_map_lua
// ============================================================================

// on.<event>(fn): append fn to _registered_hooks[event]
static int register_handler(lua_State* L)
{
    const char* event = lua_tostring(L, lua_upvalueindex(1));
    luaL_checktype(L, 1, LUA_TFUNCTION);

    lua_getglobal(L, "_registered_hooks");          // [-0, +1] hooks table
    lua_getfield(L, -1, event);                     // hooks[event]
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, event);                 // hooks[event] = {}
    }
    int n = (int)lua_rawlen(L, -1);
    lua_pushvalue(L, 1);                            // fn
    lua_rawseti(L, -2, n + 1);
    lua_pop(L, 2);                                  // pop hooks[event] + hooks
    return 0;
}

// __index metamethod for 'on' table: returns a register_handler closure
static int on_index(lua_State* L)
{
    // L[1]=on table, L[2]=key e.g. "map_load"
    // Prepend "on_" so on.map_load registers under "on_map_load"
    const char* key = luaL_checkstring(L, 2);
    char event[64];
    snprintf(event, sizeof(event), "on_%s", key);
    lua_pushstring(L, event);
    lua_pushcclosure(L, register_handler, 1);
    return 1;
}

static void inject_hook_registry(lua_State* L)
{
    // _registered_hooks = {}
    lua_newtable(L);
    lua_setglobal(L, "_registered_hooks");

    // on = setmetatable({}, {__index = on_index})
    lua_newtable(L);              // on table
    lua_newtable(L);              // metatable
    lua_pushcfunction(L, on_index);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setglobal(L, "on");
}

// ============================================================================
// C command wrapper — called by the server command system when a
// Lua-registered command is invoked.
// ============================================================================

static void lua_cmd_execute(void* p_server, command_args_t arguments)
{
    (void)p_server;

    if (arguments.argc == 0 || !arguments.argv[0]) {
        return;
    }
    const char* cmd_name = arguments.argv[0];

    // Search server commands, then map commands.
    lua_cmd_entry_t* entry = g_server_cmds;
    while (entry) {
        if (strncmp(entry->name, cmd_name, sizeof(entry->name)) == 0) {
            break;
        }
        entry = entry->next;
    }
    if (!entry) {
        entry = g_map_cmds;
        while (entry) {
            if (strncmp(entry->name, cmd_name, sizeof(entry->name)) == 0) {
                break;
            }
            entry = entry->next;
        }
    }
    if (!entry) {
        return;
    }

    lua_State* L = entry->L;

    // Stack: push function, then player_id, then args table.
    lua_rawgeti(L, LUA_REGISTRYINDEX, entry->func_ref);

    if (arguments.player) {
        lua_pushinteger(L, arguments.player->id);
    } else {
        lua_pushinteger(L, -1); // console
    }

    lua_newtable(L);
    for (uint32_t i = 1; i < arguments.argc; i++) {
        lua_pushstring(L, arguments.argv[i]);
        lua_rawseti(L, -2, (lua_Integer)i);
    }

    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        LOG_ERROR("[Script] Command '%s' handler error: %s",
                  cmd_name, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// ============================================================================
// Internal helpers
// ============================================================================

static void store_server(lua_State* L, server_t* server)
{
    lua_pushlightuserdata(L, (void*)&g_server_key);
    lua_pushlightuserdata(L, server);
    lua_settable(L, LUA_REGISTRYINDEX);
}

server_t* lua_mgr_get_server(lua_State* L)
{
    lua_pushlightuserdata(L, (void*)&g_server_key);
    lua_gettable(L, LUA_REGISTRYINDEX);
    server_t* server = (server_t*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return server;
}

static lua_State* new_state(server_t* server)
{
    lua_State* L = luaL_newstate();
    if (!L) {
        return NULL;
    }
    luaL_openlibs(L);
    store_server(L, server);
    lua_bindings_register(L, server);
    return L;
}

static void load_scripts(lua_State* L, const char* dir, char** scripts, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!scripts[i]) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, scripts[i]);
        if (luaL_dofile(L, path) != LUA_OK) {
            LOG_ERROR("[Scripting] Error loading %s: %s", path, lua_tostring(L, -1));
            lua_pop(L, 1);
        } else {
            LOG_INFO("[Scripting] Loaded: %s", path);
        }
    }
}

// Unregister and free all entries in a command list.
static void unregister_cmds(server_t* server, lua_cmd_entry_t** list)
{
    lua_cmd_entry_t* cmd;
    lua_cmd_entry_t* tmp;

    LL_FOREACH_SAFE(*list, cmd, tmp) {
        command_t* scmd = NULL;
        HASH_FIND_STR(server->cmds_map, cmd->name, scmd);
        if (scmd) {
            HASH_DEL(server->cmds_map, scmd);
            LL_DELETE(server->cmds_list, scmd);
            free(scmd);
        }
        if (cmd->L) {
            luaL_unref(cmd->L, LUA_REGISTRYINDEX, cmd->func_ref);
        }
        LL_DELETE(*list, cmd);
        free(cmd);
    }
    *list = NULL;
}

// ============================================================================
// Public lifecycle
// ============================================================================

void lua_script_manager_init(server_t* server, char** scripts, size_t count)
{
    g_server = server;

    g_server_lua = new_state(server);
    if (!g_server_lua) {
        LOG_ERROR("[Scripting] Failed to create server Lua state");
        return;
    }

    load_scripts(g_server_lua, "scripts", scripts, count);
    LOG_INFO("[Scripting] Server scripting initialized");
}

void lua_script_manager_shutdown(server_t* server)
{
    // Unload map state first (releases map commands).
    lua_script_manager_map_unload(server, NULL);

    unregister_cmds(server, &g_server_cmds);

    if (g_server_lua) {
        lua_close(g_server_lua);
        g_server_lua = NULL;
    }

    g_server = NULL;
    LOG_INFO("[Scripting] Server scripting shut down");
}

void lua_script_manager_map_load(server_t* server, const char* map_name,
                                  char** scripts, size_t count)
{
    if (!map_name) return;

    if (!scripts || count == 0) {
        LOG_INFO("[Scripting] No scripts for map: %s", map_name);
        return;
    }

    g_map_lua = new_state(server);
    if (!g_map_lua) {
        LOG_ERROR("[Scripting] Failed to create map Lua state for: %s", map_name);
        return;
    }

    inject_hook_registry(g_map_lua);

    for (size_t i = 0; i < count; i++) {
        if (!scripts[i]) continue;
        char path[256];
        snprintf(path, sizeof(path), "map_scripts/%s", scripts[i]);
        if (luaL_dofile(g_map_lua, path) == LUA_OK) {
            LOG_INFO("[Scripting] Loaded map script: %s", path);
        } else {
            const char* err = lua_tostring(g_map_lua, -1);
            LOG_ERROR("[Scripting] Error in map script %s: %s",
                      path, err ? err : "unknown error");
            lua_pop(g_map_lua, 1);
        }
    }
}

void lua_script_manager_map_unload(server_t* server, const char* map_name)
{
    unregister_cmds(server, &g_map_cmds);

    // Clear controllers owned by the map Lua state before closing it,
    // to prevent dangling lua_State* pointers on any surviving bots.
    if (g_map_lua && g_server) {
        player_t *p, *tmp;
        HASH_ITER(hh, g_server->players, p, tmp) {
            if (p->is_bot && p->controller_L == (void*)g_map_lua) {
                if (p->lua_controller_update_ref != LUA_NOREF) {
                    luaL_unref(g_map_lua, LUA_REGISTRYINDEX, p->lua_controller_update_ref);
                    p->lua_controller_update_ref = LUA_NOREF;
                }
                luaL_unref(g_map_lua, LUA_REGISTRYINDEX, p->lua_controller_ref);
                p->controller_L       = NULL;
                p->lua_controller_ref = LUA_NOREF;
            }
        }
    }

    if (g_map_lua) {
        lua_close(g_map_lua);
        g_map_lua = NULL;
    }

    if (map_name) {
        LOG_INFO("[Scripting] Map scripting unloaded for: %s", map_name);
    }
}

// ============================================================================
// Command registration (called from LuaBindings.c)
// ============================================================================

void lua_mgr_register_command(lua_State* L, int func_ref,
                               const char* name, const char* description,
                               uint32_t permissions)
{
    if (!g_server || !L || !name) {
        return;
    }

    // Reject names that exceed the fixed buffer in lua_cmd_entry_t.
    if (strlen(name) >= sizeof(((lua_cmd_entry_t*)0)->name)) {
        LOG_ERROR("[Scripting] Command name too long (max %zu chars): %s",
                  sizeof(((lua_cmd_entry_t*)0)->name) - 1, name);
        luaL_unref(L, LUA_REGISTRYINDEX, func_ref);
        return;
    }

    // Reject if a command with this name already exists.
    command_t* existing = NULL;
    HASH_FIND_STR(g_server->cmds_map, name, existing);
    if (existing) {
        LOG_WARNING("[Scripting] Command already registered: %s", name);
        luaL_unref(L, LUA_REGISTRYINDEX, func_ref);
        return;
    }

    // Track for cleanup on state close.
    lua_cmd_entry_t* entry = spadesx_malloc(sizeof(lua_cmd_entry_t));
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->L        = L;
    entry->func_ref = func_ref;
    entry->next     = NULL;

    if (L == g_map_lua) {
        LL_APPEND(g_map_cmds, entry);
    } else {
        LL_APPEND(g_server_cmds, entry);
    }

    // Create and register the server-side command_t.
    command_t* cmd = spadesx_malloc(sizeof(command_t));
    cmd->execute    = lua_cmd_execute;
    cmd->parse_args = 1;
    cmd->permissions = permissions;
    cmd->next       = NULL;

    strncpy(cmd->id, name, sizeof(cmd->id) - 1);
    cmd->id[sizeof(cmd->id) - 1] = '\0';

    strncpy(cmd->description, description, sizeof(cmd->description) - 1);
    cmd->description[sizeof(cmd->description) - 1] = '\0';

    HASH_ADD_STR(g_server->cmds_map, id, cmd);
    LL_APPEND(g_server->cmds_list, cmd);

    LOG_INFO("[Scripting] Registered command: %s", name);
}

// ============================================================================
// Dispatch primitives — shared error handling and table retrieval
// ============================================================================

// Prepare a named-global call: push the function and return 1.
// Returns 0 (stack unchanged) if L is NULL or the global is not callable.
static int dispatch_begin(lua_State* L, const char* fn)
{
    if (!L) return 0;
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    return 1;
}

// Finish a void call: pcall nargs args, log any error, clean stack.
static void dispatch_void_call(lua_State* L, const char* fn, int nargs)
{
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Prepare a hooks iteration: get _registered_hooks[event] and return its length.
// Returns 0 (stack clean) if L is NULL, event absent, or list empty.
// On success leaves [_registered_hooks, handlers_table] on top of stack.
static int hooks_begin(lua_State* L, const char* event)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    if (n == 0) { lua_pop(L, 2); return 0; }
    return n;
}

// pcall one hook handler (already on stack), log and discard any error.
static void hooks_iter_void(lua_State* L, const char* event, int i, int nargs)
{
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// ============================================================================
// Hook dispatch helpers — named globals (used for g_server_lua / init.lua)
// ============================================================================

static void dispatch_void_0(lua_State* L, const char* fn)
{
    if (!dispatch_begin(L, fn)) return;
    dispatch_void_call(L, fn, 0);
}

static void dispatch_void_1s(lua_State* L, const char* fn, const char* s)
{
    if (!dispatch_begin(L, fn)) return;
    lua_pushstring(L, s ? s : "");
    dispatch_void_call(L, fn, 1);
}

static void dispatch_void_1i(lua_State* L, const char* fn, lua_Integer i)
{
    if (!dispatch_begin(L, fn)) return;
    lua_pushinteger(L, i);
    dispatch_void_call(L, fn, 1);
}

static void dispatch_void_1i_1s(lua_State* L, const char* fn,
                                 lua_Integer i, const char* s)
{
    if (!dispatch_begin(L, fn)) return;
    lua_pushinteger(L, i);
    lua_pushstring(L, s ? s : "");
    dispatch_void_call(L, fn, 2);
}

// Deny hook: (player_id, Vector3D) — used for grenade_explode.
// Returns 1 if the script returned false, 0 otherwise.
static int dispatch_deny_1i_3f(lua_State* L, const char* fn,
                                 lua_Integer pid,
                                 lua_Number x, lua_Number y, lua_Number z)
{
    if (!L) return 0;
    int base = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    lua_pushinteger(L, pid);
    lua_push_vec3(L, (float)x, (float)y, (float)z);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_settop(L, base);
        return 0;
    }
    int denied = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_settop(L, base);
    return denied;
}

// Deny hook: (shooter_id, victim_id, hit_type, weapon) — for player_hit.
static int dispatch_deny_4i_hit(lua_State* L, const char* fn,
                                  lua_Integer shooter, lua_Integer victim,
                                  lua_Integer hit_type, lua_Integer weapon)
{
    if (!L) {
        return 0;
    }
    int base = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_pushinteger(L, shooter);
    lua_pushinteger(L, victim);
    lua_pushinteger(L, hit_type);
    lua_pushinteger(L, weapon);
    if (lua_pcall(L, 4, 1, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_settop(L, base);
        return 0;
    }
    int denied = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_settop(L, base);
    return denied;
}

// Block-place hook: (player_id, x, y, z, Color)
// Script can return false to deny, a Color to override the block color, or nothing to allow.
// Returns 1 if denied, 0 if allowed (and possibly modifies block->color).
static int dispatch_block_place(lua_State* L, const char* fn,
                                 lua_Integer pid, block_t* block)
{
    if (!L) {
        return 0;
    }
    int base = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_pushinteger(L, pid);
    lua_pushinteger(L, block->x);
    lua_pushinteger(L, block->y);
    lua_pushinteger(L, block->z);
    lua_push_color_u32(L, block->color);
    if (lua_pcall(L, 5, LUA_MULTRET, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_settop(L, base);
        return 0;
    }
    int nret = lua_gettop(L) - base;
    if (nret == 0) {
        return 0; // no return = allow as-is
    }
    if (nret >= 1 && lua_isboolean(L, base + 1)) {
        int denied = !lua_toboolean(L, base + 1);
        lua_settop(L, base);
        return denied;
    }
    lua_color_t* rc = lua_test_color(L, base + 1);
    if (rc) {
        block->color = ((uint32_t)rc->r << 16) | ((uint32_t)rc->g << 8) | rc->b;
        lua_settop(L, base);
        return 0; // allow with overridden color
    }
    lua_settop(L, base);
    return 0; // unrecognised return — allow as-is
}

// Color-change hook: (player_id, Color)
// Script can return false to deny, a Color to override, or nothing to allow.
static int dispatch_color_change(lua_State* L, const char* fn,
                                   lua_Integer pid, uint32_t* new_color)
{
    if (!L) {
        return 0;
    }
    int base = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_pushinteger(L, pid);
    lua_push_color_u32(L, *new_color);
    if (lua_pcall(L, 2, LUA_MULTRET, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_settop(L, base);
        return 0;
    }
    int nret = lua_gettop(L) - base;
    if (nret == 0) {
        return 0;
    }
    if (nret >= 1 && lua_isboolean(L, base + 1)) {
        int denied = !lua_toboolean(L, base + 1);
        lua_settop(L, base);
        return denied;
    }
    lua_color_t* rc = lua_test_color(L, base + 1);
    if (rc) {
        *new_color = ((uint32_t)rc->r << 16) | ((uint32_t)rc->g << 8) | rc->b;
        lua_settop(L, base);
        return 0;
    }
    lua_settop(L, base);
    return 0;
}

// Command hook: (player_id, command) — returns 1 if handled.
static int dispatch_command(lua_State* L, const char* fn,
                              lua_Integer pid, const char* cmd)
{
    if (!L) {
        return 0;
    }
    int base = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_pushinteger(L, pid);
    lua_pushstring(L, cmd ? cmd : "");
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_settop(L, base);
        return 0;
    }
    int handled = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    lua_settop(L, base);
    return handled;
}

// ============================================================================
// Map hook dispatch helpers — iterate _registered_hooks[event]
// ============================================================================

static void dispatch_hooks_void_0(lua_State* L, const char* event)
{
    int n = hooks_begin(L, event);
    if (!n) return;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        hooks_iter_void(L, event, i, 0);
    }
    lua_pop(L, 2);
}

static void dispatch_hooks_void_1s(lua_State* L, const char* event, const char* s)
{
    int n = hooks_begin(L, event);
    if (!n) return;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushstring(L, s ? s : "");
        hooks_iter_void(L, event, i, 1);
    }
    lua_pop(L, 2);
}

static void dispatch_hooks_void_1i(lua_State* L, const char* event, lua_Integer a)
{
    int n = hooks_begin(L, event);
    if (!n) return;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, a);
        hooks_iter_void(L, event, i, 1);
    }
    lua_pop(L, 2);
}

static void dispatch_void_3i(lua_State* L, const char* fn,
                              lua_Integer a, lua_Integer b, lua_Integer c)
{
    if (!dispatch_begin(L, fn)) return;
    lua_pushinteger(L, a);
    lua_pushinteger(L, b);
    lua_pushinteger(L, c);
    dispatch_void_call(L, fn, 3);
}

static void dispatch_hooks_void_3i(lua_State* L, const char* event,
                                    lua_Integer a, lua_Integer b, lua_Integer c)
{
    int n = hooks_begin(L, event);
    if (!n) return;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, a);
        lua_pushinteger(L, b);
        lua_pushinteger(L, c);
        hooks_iter_void(L, event, i, 3);
    }
    lua_pop(L, 2);
}

static void dispatch_hooks_void_1i_1s(lua_State* L, const char* event,
                                       lua_Integer a, const char* s)
{
    int n = hooks_begin(L, event);
    if (!n) return;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, a);
        lua_pushstring(L, s ? s : "");
        hooks_iter_void(L, event, i, 2);
    }
    lua_pop(L, 2);
}

// Returns 1 if any handler returned false (deny), 0 otherwise.
static int dispatch_hooks_deny_1i_3f(lua_State* L, const char* event,
                                       lua_Integer pid,
                                       lua_Number x, lua_Number y, lua_Number z)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, pid);
        lua_push_vec3(L, (float)x, (float)y, (float)z);
        if (lua_pcall(L, 2, 1, 0) == LUA_OK) {
            int denied = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (denied) { lua_pop(L, 2); return 1; }
        } else {
            LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
    return 0;
}

/// Deny hook: (a, b, c, d, e) — five integer arguments.
static int dispatch_deny_5i(lua_State* L, const char* fn,
                              lua_Integer a, lua_Integer b, lua_Integer c,
                              lua_Integer d, lua_Integer e)
{
    if (!L) return 0;
    int base = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    lua_pushinteger(L, a); lua_pushinteger(L, b); lua_pushinteger(L, c);
    lua_pushinteger(L, d); lua_pushinteger(L, e);
    if (lua_pcall(L, 5, 1, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_settop(L, base);
        return 0;
    }
    int denied = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_settop(L, base);
    return denied;
}


// Returns 1 if any handler returned false (deny), 0 otherwise.
static int dispatch_hooks_deny_5i(lua_State* L, const char* event,
                                   lua_Integer a, lua_Integer b, lua_Integer c,
                                   lua_Integer d, lua_Integer e)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, a); lua_pushinteger(L, b); lua_pushinteger(L, c);
        lua_pushinteger(L, d); lua_pushinteger(L, e);
        if (lua_pcall(L, 5, 1, 0) == LUA_OK) {
            int denied = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (denied) { lua_pop(L, 2); return 1; }
        } else {
            LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
    return 0;
}

static int dispatch_hooks_deny_hit(lua_State* L, const char* event,
                                    lua_Integer shooter, lua_Integer victim,
                                    lua_Integer hit_type, lua_Integer weapon)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, shooter); lua_pushinteger(L, victim);
        lua_pushinteger(L, hit_type); lua_pushinteger(L, weapon);
        if (lua_pcall(L, 4, 1, 0) == LUA_OK) {
            int denied = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (denied) { lua_pop(L, 2); return 1; }
        } else {
            LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
    return 0;
}

// Block-place hooks version: iterates _registered_hooks["on_block_place"].
// Returns 1 if denied, 0 if allowed (and possibly modifies block->color).
// Stops at first handler that returns any value.
static int dispatch_hooks_block_place(lua_State* L, const char* event,
                                       lua_Integer pid, block_t* block)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        int base = lua_gettop(L);
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, pid);
        lua_pushinteger(L, block->x);
        lua_pushinteger(L, block->y);
        lua_pushinteger(L, block->z);
        lua_push_color_u32(L, block->color);
        if (lua_pcall(L, 5, LUA_MULTRET, 0) != LUA_OK) {
            LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
            lua_pop(L, 1);
            continue;
        }
        int nret = lua_gettop(L) - base;
        if (nret == 0) {
            continue; // no return — allow, try next handler
        }
        if (nret >= 1 && lua_isboolean(L, base + 1)) {
            int denied = !lua_toboolean(L, base + 1);
            lua_settop(L, base);
            lua_pop(L, 2); // hooks[event] + hooks
            return denied;
        }
        lua_color_t* rc = lua_test_color(L, base + 1);
        if (rc) {
            block->color = ((uint32_t)rc->r << 16) | ((uint32_t)rc->g << 8) | rc->b;
            lua_settop(L, base);
            lua_pop(L, 2); // hooks[event] + hooks
            return 0; // allow with overridden color
        }
        // unrecognised return — allow, continue to next handler
        lua_settop(L, base);
    }
    lua_pop(L, 2); // hooks[event] + hooks
    return 0;
}

// Color-change hooks version: iterates _registered_hooks["on_color_change"].
// Returns 1 if denied, 0 if allowed (and possibly modifies *new_color).
// Stops at first handler that returns any value.
static int dispatch_hooks_color_change(lua_State* L, const char* event,
                                        lua_Integer pid, uint32_t* new_color)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        int base = lua_gettop(L);
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, pid);
        lua_push_color_u32(L, *new_color);
        if (lua_pcall(L, 2, LUA_MULTRET, 0) != LUA_OK) {
            LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
            lua_pop(L, 1);
            continue;
        }
        int nret = lua_gettop(L) - base;
        if (nret == 0) {
            continue;
        }
        if (nret >= 1 && lua_isboolean(L, base + 1)) {
            int denied = !lua_toboolean(L, base + 1);
            lua_settop(L, base);
            lua_pop(L, 2); // hooks[event] + hooks
            return denied;
        }
        lua_color_t* rc = lua_test_color(L, base + 1);
        if (rc) {
            *new_color = ((uint32_t)rc->r << 16) | ((uint32_t)rc->g << 8) | rc->b;
            lua_settop(L, base);
            lua_pop(L, 2); // hooks[event] + hooks
            return 0; // allow with overridden color
        }
        // unrecognised return — allow, continue
        lua_settop(L, base);
    }
    lua_pop(L, 2); // hooks[event] + hooks
    return 0;
}

// Command hooks version: iterates _registered_hooks["on_command"].
// Returns 1 if any handler returned true (handled), 0 otherwise.
static int dispatch_hooks_command(lua_State* L, const char* event,
                                   lua_Integer pid, const char* cmd)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, pid);
        lua_pushstring(L, cmd ? cmd : "");
        if (lua_pcall(L, 2, 1, 0) == LUA_OK) {
            int handled = lua_isboolean(L, -1) && lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (handled) { lua_pop(L, 2); return 1; }
        } else {
            LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
    return 0;
}

// (player_id, team) — used for intel_take and intel_capture.
static void dispatch_void_2i(lua_State* L, const char* fn,
                              lua_Integer a, lua_Integer b)
{
    if (!dispatch_begin(L, fn)) return;
    lua_pushinteger(L, a);
    lua_pushinteger(L, b);
    dispatch_void_call(L, fn, 2);
}

static void dispatch_hooks_void_2i(lua_State* L, const char* event,
                                    lua_Integer a, lua_Integer b)
{
    int n = hooks_begin(L, event);
    if (!n) return;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, a);
        lua_pushinteger(L, b);
        hooks_iter_void(L, event, i, 2);
    }
    lua_pop(L, 2);
}

// (player_id, team, Vector3D) — used for intel_drop.
static void dispatch_void_2i_vec3(lua_State* L, const char* fn,
                                   lua_Integer a, lua_Integer b,
                                   float x, float y, float z)
{
    if (!dispatch_begin(L, fn)) return;
    lua_pushinteger(L, a);
    lua_pushinteger(L, b);
    lua_push_vec3(L, x, y, z);
    dispatch_void_call(L, fn, 3);
}

static void dispatch_hooks_void_2i_vec3(lua_State* L, const char* event,
                                         lua_Integer a, lua_Integer b,
                                         float x, float y, float z)
{
    int n = hooks_begin(L, event);
    if (!n) return;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, a);
        lua_pushinteger(L, b);
        lua_push_vec3(L, x, y, z);
        hooks_iter_void(L, event, i, 3);
    }
    lua_pop(L, 2);
}

// Spawn hook: (player_id) → optional Vector3D to override spawn position.
// Returns 1 and writes to *out_pos if the script returned a Vector3D, else 0.
static int dispatch_spawn_1i(lua_State* L, const char* fn,
                               lua_Integer pid, vector3f_t* out_pos)
{
    if (!L) return 0;
    int base = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    lua_pushinteger(L, pid);
    if (lua_pcall(L, 1, LUA_MULTRET, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_settop(L, base);
        return 0;
    }
    int nret = lua_gettop(L) - base;
    if (nret >= 1) {
        lua_vec3_t* v = lua_test_vec3(L, base + 1);
        if (v) {
            out_pos->x = v->x;
            out_pos->y = v->y;
            out_pos->z = v->z;
            lua_settop(L, base);
            return 1;
        }
    }
    lua_settop(L, base);
    return 0;
}

static int dispatch_hooks_spawn_1i(lua_State* L, const char* event,
                                     lua_Integer pid, vector3f_t* out_pos)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        int base = lua_gettop(L);
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, pid);
        if (lua_pcall(L, 1, LUA_MULTRET, 0) != LUA_OK) {
            LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
            lua_pop(L, 1);
            continue;
        }
        int nret = lua_gettop(L) - base;
        if (nret >= 1) {
            lua_vec3_t* v = lua_test_vec3(L, base + 1);
            if (v) {
                out_pos->x = v->x;
                out_pos->y = v->y;
                out_pos->z = v->z;
                lua_settop(L, base);
                lua_pop(L, 2);
                return 1;
            }
        }
        lua_settop(L, base);
    }
    lua_pop(L, 2);
    return 0;
}

// Block-line deny hook: (player_id, start_vec3, end_vec3).
// Returns 1 if denied, 0 otherwise.
static int dispatch_deny_block_line(lua_State* L, const char* fn,
                                     lua_Integer pid,
                                     vector3i_t start, vector3i_t end)
{
    if (!L) return 0;
    int base = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    lua_pushinteger(L, pid);
    lua_push_vec3(L, (float)start.x, (float)start.y, (float)start.z);
    lua_push_vec3(L, (float)end.x,   (float)end.y,   (float)end.z);
    if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
        LOG_ERROR("[Script] %s: %s", fn, lua_tostring(L, -1));
        lua_settop(L, base);
        return 0;
    }
    int denied = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_settop(L, base);
    return denied;
}

static int dispatch_hooks_deny_block_line(lua_State* L, const char* event,
                                           lua_Integer pid,
                                           vector3i_t start, vector3i_t end)
{
    if (!L) return 0;
    lua_getglobal(L, "_registered_hooks");
    lua_getfield(L, -1, event);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushinteger(L, pid);
        lua_push_vec3(L, (float)start.x, (float)start.y, (float)start.z);
        lua_push_vec3(L, (float)end.x,   (float)end.y,   (float)end.z);
        if (lua_pcall(L, 3, 1, 0) == LUA_OK) {
            int denied = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (denied) { lua_pop(L, 2); return 1; }
        } else {
            LOG_ERROR("[Script] %s[%d]: %s", event, i, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
    return 0;
}

// ============================================================================
// Hook dispatchers (public, called from ScriptingAPI.c)
// ============================================================================

void lua_hook_server_init(server_t* server)
{
    (void)server;
    dispatch_void_0(g_server_lua, "on_server_init");
    dispatch_hooks_void_0(g_map_lua, "on_server_init");
}

void lua_hook_server_shutdown(server_t* server)
{
    (void)server;
    dispatch_void_0(g_server_lua, "on_server_shutdown");
    dispatch_hooks_void_0(g_map_lua, "on_server_shutdown");
}

void lua_hook_tick(server_t* server)
{
    dispatch_void_0(g_server_lua, "on_tick");
    dispatch_hooks_void_0(g_map_lua, "on_tick");

    // Dispatch bot controllers.
    //
    // The update function and (for table controllers) the self reference are
    // resolved once at set_controller time and stored as registry refs.
    // This loop therefore does two rawgeti calls + pcall per bot with no
    // per-tick string table lookups or type checks beyond what is strictly needed.
    player_t *p, *tmp;
    HASH_ITER(hh, server->players, p, tmp) {
        if (!p->is_bot || p->state != STATE_READY || !p->controller_L) {
            continue;
        }
        if (p->lua_controller_ref == LUA_NOREF) {
            continue;
        }
        lua_State* L = (lua_State*)p->controller_L;
        if (p->lua_controller_update_ref != LUA_NOREF) {
            // Table controller: call update(self, id)
            lua_rawgeti(L, LUA_REGISTRYINDEX, p->lua_controller_update_ref); // update fn
            lua_rawgeti(L, LUA_REGISTRYINDEX, p->lua_controller_ref);        // self
            lua_pushinteger(L, p->id);
            if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                LOG_ERROR("[Bot] controller:update error (id=%d): %s",
                          p->id, lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        } else {
            // Function controller: call fn(id)
            lua_rawgeti(L, LUA_REGISTRYINDEX, p->lua_controller_ref);
            lua_pushinteger(L, p->id);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                LOG_ERROR("[Bot] controller error (id=%d): %s",
                          p->id, lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
    }
}

void lua_hook_player_connect(server_t* server, player_t* player)
{
    (void)server;
    dispatch_void_1i(g_server_lua, "on_player_connect", player->id);
    dispatch_hooks_void_1i(g_map_lua, "on_player_connect", player->id);
}

void lua_hook_player_disconnect(server_t* server, player_t* player, const char* reason)
{
    (void)server;
    dispatch_void_1i_1s(g_server_lua, "on_player_disconnect", player->id, reason);
    dispatch_hooks_void_1i_1s(g_map_lua, "on_player_disconnect", player->id, reason);
}

void lua_hook_player_kill(server_t* server, player_t* killer, player_t* victim,
                          uint8_t kill_reason)
{
    (void)server;
    dispatch_void_3i(g_server_lua, "on_player_kill",
                     killer->id, victim->id, (lua_Integer)kill_reason);
    dispatch_hooks_void_3i(g_map_lua, "on_player_kill",
                           killer->id, victim->id, (lua_Integer)kill_reason);
}

void lua_hook_map_load(server_t* server, const char* map_name)
{
    (void)server;
    dispatch_void_1s(g_server_lua, "on_map_load", map_name);
    dispatch_hooks_void_1s(g_map_lua, "on_map_load", map_name);
}

void lua_hook_map_unload(server_t* server, const char* map_name)
{
    (void)server;
    dispatch_void_1s(g_server_lua, "on_map_unload", map_name);
    dispatch_hooks_void_1s(g_map_lua, "on_map_unload", map_name);
}

int lua_hook_grenade_explode(server_t* server, player_t* player, vector3f_t pos)
{
    (void)server;
    if (dispatch_deny_1i_3f(g_server_lua, "on_grenade_explode",
                             player->id, (lua_Number)pos.x, (lua_Number)pos.y, (lua_Number)pos.z)) {
        return SCRIPTING_DENY;
    }
    if (dispatch_hooks_deny_1i_3f(g_map_lua, "on_grenade_explode",
                                   player->id, (lua_Number)pos.x, (lua_Number)pos.y, (lua_Number)pos.z)) {
        return SCRIPTING_DENY;
    }
    return SCRIPTING_ALLOW;
}

int lua_hook_block_place(server_t* server, player_t* player, block_t* block)
{
    (void)server;
    if (dispatch_block_place(g_server_lua, "on_block_place", player->id, block)) {
        return SCRIPTING_DENY;
    }
    if (dispatch_hooks_block_place(g_map_lua, "on_block_place", player->id, block)) {
        return SCRIPTING_DENY;
    }
    return SCRIPTING_ALLOW;
}

int lua_hook_block_destroy(server_t* server, player_t* player, tool_t tool, block_t* block)
{
    (void)server;
    if (dispatch_deny_5i(g_server_lua, "on_block_destroy",
                         player->id, block->x, block->y, block->z, (lua_Integer)tool)) {
        return SCRIPTING_DENY;
    }
    if (dispatch_hooks_deny_5i(g_map_lua, "on_block_destroy",
                                player->id, block->x, block->y, block->z, (lua_Integer)tool)) {
        return SCRIPTING_DENY;
    }
    return SCRIPTING_ALLOW;
}

int lua_hook_player_hit(server_t* server, player_t* shooter, player_t* victim,
                         uint8_t hit_type, uint8_t weapon)
{
    (void)server;
    if (dispatch_deny_4i_hit(g_server_lua, "on_player_hit",
                              shooter->id, victim->id, hit_type, weapon)) {
        return SCRIPTING_DENY;
    }
    if (dispatch_hooks_deny_hit(g_map_lua, "on_player_hit",
                                 shooter->id, victim->id, hit_type, weapon)) {
        return SCRIPTING_DENY;
    }
    return SCRIPTING_ALLOW;
}

int lua_hook_color_change(server_t* server, player_t* player, uint32_t* new_color)
{
    (void)server;
    if (dispatch_color_change(g_server_lua, "on_color_change", player->id, new_color)) {
        return SCRIPTING_DENY;
    }
    if (dispatch_hooks_color_change(g_map_lua, "on_color_change", player->id, new_color)) {
        return SCRIPTING_DENY;
    }
    return SCRIPTING_ALLOW;
}

int lua_hook_command(server_t* server, player_t* player, const char* command)
{
    (void)server;
    lua_Integer pid = player ? (lua_Integer)player->id : -1;
    if (dispatch_command(g_server_lua, "on_command", pid, command)) {
        return SCRIPTING_CMD_HANDLED;
    }
    if (dispatch_hooks_command(g_map_lua, "on_command", pid, command)) {
        return SCRIPTING_CMD_HANDLED;
    }
    return SCRIPTING_CMD_PASS;
}

void lua_hook_intel_take(server_t* server, player_t* player, uint8_t team)
{
    (void)server;
    dispatch_void_2i(g_server_lua, "on_intel_take", player->id, team);
    dispatch_hooks_void_2i(g_map_lua, "on_intel_take", player->id, team);
}

void lua_hook_intel_drop(server_t* server, player_t* player, uint8_t team,
                          float x, float y, float z)
{
    (void)server;
    dispatch_void_2i_vec3(g_server_lua, "on_intel_drop", player->id, team, x, y, z);
    dispatch_hooks_void_2i_vec3(g_map_lua, "on_intel_drop", player->id, team, x, y, z);
}

void lua_hook_intel_capture(server_t* server, player_t* player, uint8_t team)
{
    (void)server;
    dispatch_void_2i(g_server_lua, "on_intel_capture", player->id, team);
    dispatch_hooks_void_2i(g_map_lua, "on_intel_capture", player->id, team);
}

void lua_hook_player_spawn(server_t* server, player_t* player)
{
    (void)server;
    vector3f_t new_pos = player->movement.position;
    if (dispatch_spawn_1i(g_server_lua, "on_player_spawn", player->id, &new_pos)) {
        player->movement.position = new_pos;
        return;
    }
    if (dispatch_hooks_spawn_1i(g_map_lua, "on_player_spawn", player->id, &new_pos)) {
        player->movement.position = new_pos;
    }
}

int lua_hook_block_line(server_t* server, player_t* player,
                         vector3i_t start, vector3i_t end)
{
    (void)server;
    if (dispatch_deny_block_line(g_server_lua, "on_block_line", player->id, start, end)) {
        return SCRIPTING_DENY;
    }
    if (dispatch_hooks_deny_block_line(g_map_lua, "on_block_line", player->id, start, end)) {
        return SCRIPTING_DENY;
    }
    return SCRIPTING_ALLOW;
}
