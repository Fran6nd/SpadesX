// LuaBindings.c - Lua 5.4 scripting API modules
//
// Registers four global modules into each Lua state:
//   player  — query and manipulate players
//   map     — query and manipulate the voxel map
//   server  — server-level operations (broadcast, register_command)
//   log     — logging at various severity levels

#include <Server/Scripting/Lua/LuaBindings.h>
#include <Server/Scripting/Lua/LuaScriptManager.h>
#include <Server/Scripting/Lua/LuaTypes.h>
#include <Server/Scripting/ScriptingAPI.h>

#include <Server/Structs/ServerStruct.h>
#include <Server/Structs/PlayerStruct.h>
#include <Server/Player.h>
#include <Server/Packets/Packets.h>
#include <Server/Map.h>
#include <Util/Checks/PlayerChecks.h>
#include <Util/Log.h>
#include <Util/Nanos.h>
#include <Util/Weapon.h>
#include <Util/Notice.h>
#include <Util/Uthash.h>
#include <Util/Enums.h>

#include <libmapvxl/libmapvxl.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <math.h>
#include <string.h>

// ============================================================================
// Helpers
// ============================================================================

// Retrieve the server and look up a player by id.
// Pushes nil and returns NULL if the player is not found.
static player_t* get_player_arg(lua_State* L, int arg_idx)
{
    lua_Integer id = luaL_checkinteger(L, arg_idx);
    server_t* server = lua_mgr_get_server(L);
    if (!server) {
        return NULL;
    }
    uint8_t pid = (uint8_t)id;
    player_t* player = NULL;
    HASH_FIND(hh, server->players, &pid, sizeof(pid), player);
    return player;
}

// ============================================================================
// player module
// ============================================================================

static int l_player_count(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    lua_pushinteger(L, server ? server->protocol.num_players : 0);
    return 1;
}

static int l_player_get_name(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    lua_pushstring(L, p ? p->name : "");
    return 1;
}

static int l_player_get_team(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, p->team);
    return 1;
}

static int l_player_get_weapon(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, p->weapon);
    return 1;
}

// player.set_team(bot_id, team) — change a bot's team and push the update to
// all connected clients (ExistingPlayer for metadata, CreatePlayer if alive).
static int l_bot_set_team(lua_State* L)
{
    server_t* server   = lua_mgr_get_server(L);
    player_t* p        = get_player_arg(L, 1);
    lua_Integer team   = luaL_checkinteger(L, 2);
    if (!server || !p || !p->is_bot) {
        return 0;
    }
    if (team != TEAM_A && team != TEAM_B && team != TEAM_SPECTATOR) {
        return 0;
    }
    p->team = (uint8_t)team;

    if (p->state == STATE_READY) {
        player_t *r, *tmp;
        HASH_ITER(hh, server->players, r, tmp) {
            if (r != p && !r->is_bot && is_past_join_screen(r)) {
                send_existing_player(server, r, p);
                send_create_player(server, r, p);
            }
        }
    }
    return 0;
}

// player.set_weapon(bot_id, weapon) — change a bot's weapon and push the
// update to all connected clients.
static int l_bot_set_weapon(lua_State* L)
{
    server_t* server    = lua_mgr_get_server(L);
    player_t* p         = get_player_arg(L, 1);
    lua_Integer weapon  = luaL_checkinteger(L, 2);
    if (!server || !p || !p->is_bot) {
        return 0;
    }
    if (weapon > 2) {
        return 0;
    }
    p->weapon = (uint8_t)weapon;
    set_default_player_ammo(p);

    if (p->state == STATE_READY) {
        player_t *r, *tmp;
        HASH_ITER(hh, server->players, r, tmp) {
            if (r != p && !r->is_bot && is_past_join_screen(r)) {
                send_existing_player(server, r, p);
                send_create_player(server, r, p);
            }
        }
    }
    return 0;
}

static int l_player_get_hp(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    lua_pushinteger(L, p ? p->hp : 0);
    return 1;
}

static int l_player_set_hp(lua_State* L)
{
    player_t* p  = get_player_arg(L, 1);
    lua_Integer hp = luaL_checkinteger(L, 2);
    if (p && hp >= 0 && hp <= 100) {
        p->hp = (uint8_t)hp;
        if (hp == 0) {
            p->alive = 0;
        }
    }
    return 0;
}

static int l_player_get_position(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    lua_push_vec3(L,
        p->movement.position.x,
        p->movement.position.y,
        p->movement.position.z);
    return 1;
}

static int l_player_set_position(lua_State* L)
{
    player_t*   p = get_player_arg(L, 1);
    lua_vec3_t* v = lua_check_vec3(L, 2);
    if (p && v->x >= 0 && v->x < 512 && v->y >= 0 && v->y < 512 &&
        v->z >= 0 && v->z < 64)
    {
        p->movement.position.x = v->x;
        p->movement.position.y = v->y;
        p->movement.position.z = v->z;
    }
    return 0;
}

static int l_player_get_color(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    lua_push_color_u32(L, p->tool_color.raw);
    return 1;
}

static int l_player_set_color(lua_State* L)
{
    player_t*    p = get_player_arg(L, 1);
    lua_color_t* c = lua_check_color(L, 2);
    if (p) {
        p->tool_color.raw = ((uint32_t)c->r << 16) |
                            ((uint32_t)c->g <<  8) |
                             (uint32_t)c->b;
    }
    return 0;
}

static int l_player_set_color_broadcast(lua_State* L)
{
    server_t*    server = lua_mgr_get_server(L);
    player_t*    p      = get_player_arg(L, 1);
    lua_color_t* c      = lua_check_color(L, 2);
    if (server && p) {
        color_t color;
        color.raw = ((uint32_t)c->r << 16) |
                    ((uint32_t)c->g <<  8) |
                     (uint32_t)c->b;
        p->tool_color = color;
        send_set_color_to_player(server, p, p, color);
        send_set_color(server, p, color);
    }
    return 0;
}

static int l_player_kill(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    if (p) {
        p->hp    = 0;
        p->alive = 0;
    }
    return 0;
}

static int l_player_restock(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* p      = get_player_arg(L, 1);
    if (server && p) {
        p->blocks   = 50;
        p->grenades = 3;
        send_restock(server, p);
    }
    return 0;
}

static int l_player_send_notice(lua_State* L)
{
    player_t*   p   = get_player_arg(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (p) {
        send_server_notice(p, 0, "%s", msg);
    }
    return 0;
}

static int l_player_is_bot(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    lua_pushboolean(L, p ? p->is_bot : 0);
    return 1;
}

static int l_player_get_blocks(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    lua_pushinteger(L, p ? p->blocks : 0);
    return 1;
}

static int l_player_get_grenades(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    lua_pushinteger(L, p ? p->grenades : 0);
    return 1;
}

static int l_player_get_tool(lua_State* L)
{
    player_t* p = get_player_arg(L, 1);
    lua_pushinteger(L, p ? p->item : 0);
    return 1;
}

// Iterates all connected players as (id, name) pairs.
// Usage: for id, name in player.iterate() do ... end
static int l_player_iterate_next(lua_State* L)
{
    player_t* current = (player_t*)lua_touserdata(L, lua_upvalueindex(1));
    if (!current) {
        return 0;
    }
    lua_pushlightuserdata(L, (player_t*)current->hh.next);
    lua_replace(L, lua_upvalueindex(1));

    lua_pushinteger(L, current->id);
    lua_pushstring(L, current->name);
    return 2;
}

static int l_player_iterate(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    lua_pushlightuserdata(L, server ? server->players : NULL);
    lua_pushcclosure(L, l_player_iterate_next, 1);
    return 1;
}

// player.create_bot(name, team, weapon) → id or nil
static const luaL_Reg player_lib[] = {
    {"count",               l_player_count},
    {"get_name",            l_player_get_name},
    {"get_team",            l_player_get_team},
    {"get_weapon",          l_player_get_weapon},
    {"set_team",            l_bot_set_team},
    {"set_weapon",          l_bot_set_weapon},
    {"get_hp",              l_player_get_hp},
    {"set_hp",              l_player_set_hp},
    {"get_position",        l_player_get_position},
    {"set_position",        l_player_set_position},
    {"get_color",           l_player_get_color},
    {"set_color",           l_player_set_color},
    {"set_color_broadcast", l_player_set_color_broadcast},
    {"kill",                l_player_kill},
    {"restock",             l_player_restock},
    {"send_notice",         l_player_send_notice},
    {"is_bot",              l_player_is_bot},
    {"get_blocks",          l_player_get_blocks},
    {"get_grenades",        l_player_get_grenades},
    {"get_tool",            l_player_get_tool},
    {"iterate",             l_player_iterate},
    {NULL, NULL}
};

// ============================================================================
// bot module
// ============================================================================

// Internal: store or clear the Lua controller reference on a bot.
// arg_idx is the stack index of the controller (function, table, or nil).
//
// For function controllers  : lua_controller_ref = the function;
//                             lua_controller_update_ref = LUA_NOREF.
// For table controllers     : lua_controller_ref = the table (used as self);
//                             lua_controller_update_ref = pre-resolved ref to table.update.
//   Resolving update once here avoids a per-tick lua_getfield string lookup.
static void lua_ref_controller(lua_State* L, player_t* bot, int arg_idx)
{
    // Release any existing references
    if (bot->controller_L) {
        lua_State* old_L = (lua_State*)bot->controller_L;
        if (bot->lua_controller_update_ref != LUA_NOREF) {
            luaL_unref(old_L, LUA_REGISTRYINDEX, bot->lua_controller_update_ref);
            bot->lua_controller_update_ref = LUA_NOREF;
        }
        if (bot->lua_controller_ref != LUA_NOREF) {
            luaL_unref(old_L, LUA_REGISTRYINDEX, bot->lua_controller_ref);
            bot->lua_controller_ref = LUA_NOREF;
        }
        bot->controller_L = NULL;
    }
    if (lua_isnoneornil(L, arg_idx)) {
        return; // clearing the controller
    }
    int t = lua_type(L, arg_idx);
    if (t == LUA_TFUNCTION) {
        lua_pushvalue(L, arg_idx);
        bot->lua_controller_ref        = luaL_ref(L, LUA_REGISTRYINDEX);
        bot->lua_controller_update_ref = LUA_NOREF; // dispatch uses lua_controller_ref directly
        bot->controller_L              = (void*)L;
    } else if (t == LUA_TTABLE) {
        lua_getfield(L, arg_idx, "update");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            luaL_argerror(L, arg_idx, "table controller must have an 'update' function");
            return;
        }
        bot->lua_controller_update_ref = luaL_ref(L, LUA_REGISTRYINDEX); // pop & store update fn
        lua_pushvalue(L, arg_idx);
        bot->lua_controller_ref = luaL_ref(L, LUA_REGISTRYINDEX);        // store table as self
        bot->controller_L       = (void*)L;
    } else {
        luaL_argerror(L, arg_idx, "controller must be a function or table with update(self, id)");
    }
}

// Helper: retrieve server + bot player; validates is_bot.
// Returns NULL (pushing nil) if not found or not a bot.
static player_t* get_bot_arg(lua_State* L, int arg_idx)
{
    player_t* p = get_player_arg(L, arg_idx);
    if (!p || !p->is_bot) {
        return NULL;
    }
    return p;
}

// bot.create(name) → id | nil
// Bot starts as spectator. Call bot.set_team(), bot.set_weapon(), then bot.spawn().
static int l_bot_create(lua_State* L)
{
    server_t*   server = lua_mgr_get_server(L);
    const char* name   = luaL_checkstring(L, 1);
    if (!server) {
        lua_pushnil(L);
        return 1;
    }
    player_t* bot = create_bot(server, name);
    if (!bot) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, bot->id);
    return 1;
}

// bot.set_controller(id, controller | nil)
static int l_bot_set_controller(lua_State* L)
{
    player_t* bot = get_bot_arg(L, 1);
    if (!bot) {
        return 0;
    }
    lua_ref_controller(L, bot, 2);
    return 0;
}

// bot.destroy(id)
static int l_bot_destroy(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* bot    = get_bot_arg(L, 1);
    if (server && bot) {
        destroy_bot(server, bot);
    }
    return 0;
}

// bot.spawn(id) — queue the bot for spawning via the normal state machine.
// Requires bot.set_team() to have been called with a non-spectator team first.
// The server processes STATE_SPAWNING on the next tick: sets position via
// set_player_respawn_point, broadcasts create_player via send_respawn, then
// transitions to STATE_READY. This is the same path real players take.
static int l_bot_spawn(lua_State* L)
{
    player_t* bot = get_bot_arg(L, 1);
    if (!bot) {
        return 0;
    }
    if (bot->team == TEAM_SPECTATOR) {
        return 0;  // must have a team assigned before spawning
    }
    if (bot->state == STATE_READY || bot->state == STATE_SPAWNING) {
        return 0;  // already spawned or in flight
    }
    bot->state = STATE_SPAWNING;
    return 0;
}

// bot.move(id, fwd, back, left, right)
static int l_bot_move(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* bot    = get_bot_arg(L, 1);
    if (!server || !bot) {
        return 0;
    }
    uint8_t prev_input   = bot->input;
    bot->move_forward    = lua_toboolean(L, 2) ? 1 : 0;
    bot->move_backwards  = lua_toboolean(L, 3) ? 1 : 0;
    bot->move_left       = lua_toboolean(L, 4) ? 1 : 0;
    bot->move_right      = lua_toboolean(L, 5) ? 1 : 0;
    // Repack into the raw input byte (bits 0-3: fwd/back/left/right)
    bot->input = (uint8_t)(
        (bot->move_forward   ? 1 : 0) |
        (bot->move_backwards ? 2 : 0) |
        (bot->move_left      ? 4 : 0) |
        (bot->move_right     ? 8 : 0) |
        (bot->jumping        ? 16 : 0) |
        (bot->crouching      ? 32 : 0) |
        (bot->sneaking       ? 64 : 0) |
        (bot->sprinting      ? 128 : 0)
    );
    if (bot->input != prev_input) {
        send_input_data(server, bot);
    }
    return 0;
}

// bot.look(id, Vector3D) — set forward orientation vector directly (no network send;
// the normal world-update loop broadcasts bot orientations to real players each tick).
static int l_bot_look(lua_State* L)
{
    player_t*   bot = get_bot_arg(L, 1);
    lua_vec3_t* dir = lua_check_vec3(L, 2);
    if (!bot) {
        return 0;
    }
    bot->movement.forward_orientation.x = dir->x;
    bot->movement.forward_orientation.y = dir->y;
    bot->movement.forward_orientation.z = dir->z;
    return 0;
}

// bot.lookat_point(id, Vector3D) — aim the bot toward a world-space point.
// Computes and sets the normalized forward vector; no-op if already at that point.
static int l_bot_lookat_point(lua_State* L)
{
    player_t*   bot    = get_bot_arg(L, 1);
    lua_vec3_t* target = lua_check_vec3(L, 2);
    if (!bot) {
        return 0;
    }
    float dx  = target->x - bot->movement.eye_pos.x;
    float dy  = target->y - bot->movement.eye_pos.y;
    float dz  = target->z - bot->movement.eye_pos.z;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-4f) {
        return 0;
    }
    bot->movement.forward_orientation.x = dx / len;
    bot->movement.forward_orientation.y = dy / len;
    bot->movement.forward_orientation.z = dz / len;
    return 0;
}

// bot.lookat_player(id, target_id) — aim the bot toward another player's eye position.
static int l_bot_lookat_player(lua_State* L)
{
    player_t* bot    = get_bot_arg(L, 1);
    player_t* target = get_player_arg(L, 2);
    if (!bot || !target || target->state != STATE_READY) {
        return 0;
    }
    float dx  = target->movement.eye_pos.x - bot->movement.eye_pos.x;
    float dy  = target->movement.eye_pos.y - bot->movement.eye_pos.y;
    float dz  = target->movement.eye_pos.z - bot->movement.eye_pos.z;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-4f) {
        return 0;
    }
    bot->movement.forward_orientation.x = dx / len;
    bot->movement.forward_orientation.y = dy / len;
    bot->movement.forward_orientation.z = dz / len;
    return 0;
}

// bot.jump(id)
static int l_bot_jump(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* bot    = get_bot_arg(L, 1);
    if (!server || !bot) {
        return 0;
    }
    bot->jumping = 1;
    bot->input  |= 16;
    send_input_data(server, bot);
    return 0;
}

// bot.crouch(id, enabled)
static int l_bot_crouch(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* bot    = get_bot_arg(L, 1);
    if (!server || !bot) {
        return 0;
    }
    uint8_t prev_input = bot->input;
    bot->crouching = lua_toboolean(L, 2) ? 1 : 0;
    if (bot->crouching) {
        bot->input |= 32;
    } else {
        bot->input &= (uint8_t)~32;
    }
    if (bot->input != prev_input) {
        send_input_data(server, bot);
    }
    return 0;
}

// bot.sprint(id, enabled)
static int l_bot_sprint(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* bot    = get_bot_arg(L, 1);
    if (!server || !bot) {
        return 0;
    }
    uint8_t prev_input = bot->input;
    bot->sprinting = lua_toboolean(L, 2) ? 1 : 0;
    if (bot->sprinting) {
        bot->input |= 128;
    } else {
        bot->input &= (uint8_t)~128;
    }
    if (bot->input != prev_input) {
        send_input_data(server, bot);
    }
    return 0;
}

// bot.fire(id, primary, secondary)
static int l_bot_fire(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* bot    = get_bot_arg(L, 1);
    if (!server || !bot) {
        return 0;
    }
    uint8_t prev_primary   = bot->primary_fire;
    uint8_t prev_secondary = bot->secondary_fire;
    bot->primary_fire   = lua_toboolean(L, 2) ? 1 : 0;
    bot->secondary_fire = lua_toboolean(L, 3) ? 1 : 0;
    if (bot->primary_fire != prev_primary || bot->secondary_fire != prev_secondary) {
        uint8_t wi = (uint8_t)((bot->primary_fire ? 1 : 0) | (bot->secondary_fire ? 2 : 0));
        send_weapon_input(server, bot, wi);
    }
    return 0;
}

// bot.reload(id)
static int l_bot_reload(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* bot    = get_bot_arg(L, 1);
    if (!server || !bot) {
        return 0;
    }
    if (bot->item != TOOL_GUN || bot->reloading) {
        return 0;
    }
    bot->reloading                    = 1;
    bot->timers.since_reload_start    = get_nanos();
    send_weapon_reload(server, bot, 1, bot->weapon_clip, bot->weapon_reserve);
    return 0;
}

// bot.set_tool(id, tool)
static int l_bot_set_tool(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    player_t* bot    = get_bot_arg(L, 1);
    lua_Integer tool = luaL_checkinteger(L, 2);
    if (!server || !bot) {
        return 0;
    }
    if (tool < 0 || tool > 3) {
        return 0;
    }
    bot->item = (tool_t)tool;
    send_set_tool(server, bot, (uint8_t)tool);
    return 0;
}

// bot.place_block(id, x, y, z)
static int l_bot_place_block(lua_State* L)
{
    server_t*   server = lua_mgr_get_server(L);
    player_t*   bot    = get_bot_arg(L, 1);
    lua_Integer x      = luaL_checkinteger(L, 2);
    lua_Integer y      = luaL_checkinteger(L, 3);
    lua_Integer z      = luaL_checkinteger(L, 4);
    if (!server || !bot) {
        return 0;
    }
    if (x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0 || z >= 64) {
        return 0;
    }
    if (bot->blocks == 0) {
        return 0;
    }
    bot->blocks--;
    mapvxl_set_color(&server->s_map.map, (int)x, (int)y, (int)z, bot->tool_color.raw);
    send_block_action(server, bot, BLOCKACTION_BUILD, (int)x, (int)y, (int)z);
    return 0;
}

// bot.set_team(id, team) — already implemented as l_bot_set_team above (same pointer used)
// bot.set_weapon(id, weapon) — already implemented as l_bot_set_weapon above

static const luaL_Reg bot_lib[] = {
    // lifecycle
    {"create",              l_bot_create},
    {"set_controller",      l_bot_set_controller},
    {"destroy",             l_bot_destroy},
    {"spawn",               l_bot_spawn},
    // actions
    {"move",                l_bot_move},
    {"look",                l_bot_look},
    {"lookat_point",        l_bot_lookat_point},
    {"lookat_player",       l_bot_lookat_player},
    {"jump",                l_bot_jump},
    {"crouch",              l_bot_crouch},
    {"sprint",              l_bot_sprint},
    {"fire",                l_bot_fire},
    {"reload",              l_bot_reload},
    {"place_block",         l_bot_place_block},
    // player accessors — same underlying functions, work on any player_t
    {"get_name",            l_player_get_name},
    {"get_team",            l_player_get_team},
    {"set_team",            l_bot_set_team},
    {"get_weapon",          l_player_get_weapon},
    {"set_weapon",          l_bot_set_weapon},
    {"get_hp",              l_player_get_hp},
    {"set_hp",              l_player_set_hp},
    {"get_position",        l_player_get_position},
    {"set_position",        l_player_set_position},
    {"get_color",           l_player_get_color},
    {"set_color",           l_player_set_color},
    {"set_color_broadcast", l_player_set_color_broadcast},
    {"get_tool",            l_player_get_tool},
    {"set_tool",            l_bot_set_tool},
    {"get_blocks",          l_player_get_blocks},
    {"get_grenades",        l_player_get_grenades},
    {"kill",                l_player_kill},
    {"restock",             l_player_restock},
    {"send_notice",         l_player_send_notice},
    {NULL, NULL}
};

// ============================================================================
// map module
// ============================================================================

static int l_map_get_block(lua_State* L)
{
    server_t*   server = lua_mgr_get_server(L);
    lua_Integer x      = luaL_checkinteger(L, 1);
    lua_Integer y      = luaL_checkinteger(L, 2);
    lua_Integer z      = luaL_checkinteger(L, 3);
    if (!server || x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0 || z >= 64) {
        lua_pushnil(L);
        return 1;
    }
    if (!mapvxl_is_solid(&server->s_map.map, (int)x, (int)y, (int)z)) {
        lua_pushnil(L);
        return 1;
    }
    lua_push_color_u32(L, mapvxl_get_color(&server->s_map.map, (int)x, (int)y, (int)z));
    return 1;
}

static int l_map_set_block(lua_State* L)
{
    server_t*    server = lua_mgr_get_server(L);
    lua_Integer  x      = luaL_checkinteger(L, 1);
    lua_Integer  y      = luaL_checkinteger(L, 2);
    lua_Integer  z      = luaL_checkinteger(L, 3);
    lua_color_t* c      = lua_check_color(L, 4);
    if (!server || x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0 || z >= 64) {
        return 0;
    }
    uint32_t color = ((uint32_t)c->r << 16) |
                     ((uint32_t)c->g <<  8) |
                      (uint32_t)c->b;
    mapvxl_set_color(&server->s_map.map, (int)x, (int)y, (int)z, color);
    return 0;
}

static int l_map_remove_block(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    lua_Integer x = luaL_checkinteger(L, 1);
    lua_Integer y = luaL_checkinteger(L, 2);
    lua_Integer z = luaL_checkinteger(L, 3);
    if (!server || x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0 || z >= 64) {
        return 0;
    }
    mapvxl_set_air(&server->s_map.map, (int)x, (int)y, (int)z);
    return 0;
}

static int l_map_find_top(lua_State* L)
{
    server_t* server = lua_mgr_get_server(L);
    lua_Integer x = luaL_checkinteger(L, 1);
    lua_Integer y = luaL_checkinteger(L, 2);
    if (!server) {
        lua_pushnil(L);
        return 1;
    }
    int32_t top = mapvxl_find_top_block(&server->s_map.map, (int)x, (int)y);
    if (top < 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, top);
    }
    return 1;
}

static int l_map_is_valid(lua_State* L)
{
    lua_Integer x = luaL_checkinteger(L, 1);
    lua_Integer y = luaL_checkinteger(L, 2);
    lua_Integer z = luaL_checkinteger(L, 3);
    lua_pushboolean(L, x >= 0 && x < 512 && y >= 0 && y < 512 && z >= 0 && z < 64);
    return 1;
}

static const luaL_Reg map_lib[] = {
    {"get_block",   l_map_get_block},
    {"set_block",   l_map_set_block},
    {"remove_block",l_map_remove_block},
    {"find_top",    l_map_find_top},
    {"is_valid",    l_map_is_valid},
    {NULL, NULL}
};

// ============================================================================
// server module
// ============================================================================

static int l_server_broadcast(lua_State* L)
{
    server_t*   server = lua_mgr_get_server(L);
    const char* msg    = luaL_checkstring(L, 1);
    if (server) {
        broadcast_server_notice(server, 0, "%s", msg);
    }
    return 0;
}

// server.register_command(name, description, handler [, permissions])
static int l_server_register_command(lua_State* L)
{
    const char* name  = luaL_checkstring(L, 1);
    const char* desc  = luaL_optstring(L, 2, NULL);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_Integer perms = luaL_optinteger(L, 4, 0);

    lua_pushvalue(L, 3);
    int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_mgr_register_command(L, func_ref, name, desc, (uint32_t)perms);
    return 0;
}

static const luaL_Reg server_lib[] = {
    {"broadcast",         l_server_broadcast},
    {"register_command",  l_server_register_command},
    {NULL, NULL}
};

// ============================================================================
// log module
// ============================================================================

static int l_log_debug(lua_State* L)
{
    LOG_DEBUG("[Script] %s", luaL_checkstring(L, 1));
    return 0;
}

static int l_log_info(lua_State* L)
{
    LOG_INFO("[Script] %s", luaL_checkstring(L, 1));
    return 0;
}

static int l_log_warn(lua_State* L)
{
    LOG_WARNING("[Script] %s", luaL_checkstring(L, 1));
    return 0;
}

static int l_log_error(lua_State* L)
{
    LOG_ERROR("[Script] %s", luaL_checkstring(L, 1));
    return 0;
}

static const luaL_Reg log_lib[] = {
    {"debug", l_log_debug},
    {"info",  l_log_info},
    {"warn",  l_log_warn},
    {"error", l_log_error},
    {NULL, NULL}
};

// ============================================================================
// Registration
// ============================================================================

void lua_bindings_register(lua_State* L)
{
    // Register Vector2D, Vector3D, Color globals first so they are available
    // to all subsequently loaded scripts and to the API functions themselves.
    lua_types_register(L);

    luaL_newlib(L, player_lib);
    lua_setglobal(L, "player");

    luaL_newlib(L, bot_lib);
    // Integer constants for convenience
    lua_pushinteger(L, TEAM_A);          lua_setfield(L, -2, "TEAM_A");
    lua_pushinteger(L, TEAM_B);          lua_setfield(L, -2, "TEAM_B");
    lua_pushinteger(L, TEAM_SPECTATOR);  lua_setfield(L, -2, "TEAM_SPECTATOR");
    lua_pushinteger(L, WEAPON_RIFLE);    lua_setfield(L, -2, "WEAPON_RIFLE");
    lua_pushinteger(L, WEAPON_SMG);      lua_setfield(L, -2, "WEAPON_SMG");
    lua_pushinteger(L, WEAPON_SHOTGUN);  lua_setfield(L, -2, "WEAPON_SHOTGUN");
    lua_pushinteger(L, TOOL_SPADE);      lua_setfield(L, -2, "TOOL_SPADE");
    lua_pushinteger(L, TOOL_BLOCK);      lua_setfield(L, -2, "TOOL_BLOCK");
    lua_pushinteger(L, TOOL_GUN);        lua_setfield(L, -2, "TOOL_GUN");
    lua_pushinteger(L, TOOL_GRENADE);    lua_setfield(L, -2, "TOOL_GRENADE");
    lua_setglobal(L, "bot");

    luaL_newlib(L, map_lib);
    lua_setglobal(L, "map");

    luaL_newlib(L, server_lib);
    lua_setglobal(L, "server");

    luaL_newlib(L, log_lib);
    lua_setglobal(L, "log");
}
