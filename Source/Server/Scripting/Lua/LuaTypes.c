// LuaTypes.c — Vector2D, Vector3D, Color Lua 5.4 userdata implementations.
//
// Each type is a full userdata whose payload is the C struct directly.
// Metatables live in the Lua registry under LUA_VEC2_MT / LUA_VEC3_MT /
// LUA_COLOR_MT.  Methods are stored in the metatable and reached via the
// __index handler so that field access (v.x) and method calls (v:length())
// both work without a separate lookup table.

#include <Server/Scripting/Lua/LuaTypes.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
// Push / check / test
// ============================================================================

lua_vec2_t* lua_push_vec2(lua_State* L, float x, float y)
{
    lua_vec2_t* v = (lua_vec2_t*)lua_newuserdata(L, sizeof(lua_vec2_t));
    v->x = x; v->y = y;
    luaL_setmetatable(L, LUA_VEC2_MT);
    return v;
}

lua_vec3_t* lua_push_vec3(lua_State* L, float x, float y, float z)
{
    lua_vec3_t* v = (lua_vec3_t*)lua_newuserdata(L, sizeof(lua_vec3_t));
    v->x = x; v->y = y; v->z = z;
    luaL_setmetatable(L, LUA_VEC3_MT);
    return v;
}

lua_color_t* lua_push_color(lua_State* L, uint8_t r, uint8_t g, uint8_t b)
{
    lua_color_t* c = (lua_color_t*)lua_newuserdata(L, sizeof(lua_color_t));
    c->r = r; c->g = g; c->b = b;
    luaL_setmetatable(L, LUA_COLOR_MT);
    return c;
}

lua_color_t* lua_push_color_u32(lua_State* L, uint32_t c)
{
    return lua_push_color(L,
        (uint8_t)((c >> 16) & 0xFF),
        (uint8_t)((c >>  8) & 0xFF),
        (uint8_t)( c        & 0xFF));
}

lua_vec2_t*  lua_check_vec2(lua_State* L, int idx)  { return (lua_vec2_t*) luaL_checkudata(L, idx, LUA_VEC2_MT);  }
lua_vec3_t*  lua_check_vec3(lua_State* L, int idx)  { return (lua_vec3_t*) luaL_checkudata(L, idx, LUA_VEC3_MT);  }
lua_color_t* lua_check_color(lua_State* L, int idx) { return (lua_color_t*)luaL_checkudata(L, idx, LUA_COLOR_MT); }

lua_vec2_t*  lua_test_vec2(lua_State* L, int idx)   { return (lua_vec2_t*) luaL_testudata(L, idx, LUA_VEC2_MT);   }
lua_vec3_t*  lua_test_vec3(lua_State* L, int idx)   { return (lua_vec3_t*) luaL_testudata(L, idx, LUA_VEC3_MT);   }
lua_color_t* lua_test_color(lua_State* L, int idx)  { return (lua_color_t*)luaL_testudata(L, idx, LUA_COLOR_MT);  }

// ============================================================================
// Vector3D
// ============================================================================

static int vec3_new(lua_State* L)
{
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    lua_push_vec3(L, x, y, z);
    return 1;
}

// Called via Vector3D(x,y,z) — arg 1 is the constructor table, skip it.
static int vec3_call(lua_State* L)
{
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float z = (float)luaL_checknumber(L, 4);
    lua_push_vec3(L, x, y, z);
    return 1;
}

static int vec3_index(lua_State* L)
{
    lua_vec3_t* v = luaL_checkudata(L, 1, LUA_VEC3_MT);
    const char* k = luaL_checkstring(L, 2);
    if (k[1] == '\0') {
        if (k[0] == 'x') { lua_pushnumber(L, (lua_Number)v->x); return 1; }
        if (k[0] == 'y') { lua_pushnumber(L, (lua_Number)v->y); return 1; }
        if (k[0] == 'z') { lua_pushnumber(L, (lua_Number)v->z); return 1; }
    }
    // Fall through to the metatable for method lookup
    luaL_getmetatable(L, LUA_VEC3_MT);
    lua_getfield(L, -1, k);
    lua_remove(L, -2); // remove metatable, keep value
    return 1;
}

static int vec3_newindex(lua_State* L)
{
    lua_vec3_t* v = luaL_checkudata(L, 1, LUA_VEC3_MT);
    const char* k = luaL_checkstring(L, 2);
    float val     = (float)luaL_checknumber(L, 3);
    if (k[1] == '\0') {
        if (k[0] == 'x') { v->x = val; return 0; }
        if (k[0] == 'y') { v->y = val; return 0; }
        if (k[0] == 'z') { v->z = val; return 0; }
    }
    return luaL_error(L, "Vector3D: no field '%s'", k);
}

static int vec3_tostring(lua_State* L)
{
    lua_vec3_t* v = luaL_checkudata(L, 1, LUA_VEC3_MT);
    lua_pushfstring(L, "Vector3D(%.4g, %.4g, %.4g)",
        (double)v->x, (double)v->y, (double)v->z);
    return 1;
}

static int vec3_add(lua_State* L)
{
    lua_vec3_t* a = lua_check_vec3(L, 1);
    lua_vec3_t* b = lua_check_vec3(L, 2);
    lua_push_vec3(L, a->x + b->x, a->y + b->y, a->z + b->z);
    return 1;
}

static int vec3_sub(lua_State* L)
{
    lua_vec3_t* a = lua_check_vec3(L, 1);
    lua_vec3_t* b = lua_check_vec3(L, 2);
    lua_push_vec3(L, a->x - b->x, a->y - b->y, a->z - b->z);
    return 1;
}

static int vec3_mul(lua_State* L)
{
    if (lua_type(L, 1) == LUA_TNUMBER) {
        float       s = (float)lua_tonumber(L, 1);
        lua_vec3_t* v = lua_check_vec3(L, 2);
        lua_push_vec3(L, v->x * s, v->y * s, v->z * s);
    } else {
        lua_vec3_t* v = lua_check_vec3(L, 1);
        float       s = (float)luaL_checknumber(L, 2);
        lua_push_vec3(L, v->x * s, v->y * s, v->z * s);
    }
    return 1;
}

static int vec3_unm(lua_State* L)
{
    lua_vec3_t* v = lua_check_vec3(L, 1);
    lua_push_vec3(L, -v->x, -v->y, -v->z);
    return 1;
}

static int vec3_eq(lua_State* L)
{
    lua_vec3_t* a = lua_check_vec3(L, 1);
    lua_vec3_t* b = lua_check_vec3(L, 2);
    lua_pushboolean(L, a->x == b->x && a->y == b->y && a->z == b->z);
    return 1;
}

static int vec3_length(lua_State* L)
{
    lua_vec3_t* v = lua_check_vec3(L, 1);
    lua_pushnumber(L, (lua_Number)sqrtf(v->x*v->x + v->y*v->y + v->z*v->z));
    return 1;
}

static int vec3_normalize(lua_State* L)
{
    lua_vec3_t* v   = lua_check_vec3(L, 1);
    float       len = sqrtf(v->x*v->x + v->y*v->y + v->z*v->z);
    if (len < 1e-8f) {
        lua_push_vec3(L, 0.f, 0.f, 0.f);
    } else {
        lua_push_vec3(L, v->x / len, v->y / len, v->z / len);
    }
    return 1;
}

static int vec3_dot(lua_State* L)
{
    lua_vec3_t* a = lua_check_vec3(L, 1);
    lua_vec3_t* b = lua_check_vec3(L, 2);
    lua_pushnumber(L, (lua_Number)(a->x*b->x + a->y*b->y + a->z*b->z));
    return 1;
}

static int vec3_cross(lua_State* L)
{
    lua_vec3_t* a = lua_check_vec3(L, 1);
    lua_vec3_t* b = lua_check_vec3(L, 2);
    lua_push_vec3(L,
        a->y*b->z - a->z*b->y,
        a->z*b->x - a->x*b->z,
        a->x*b->y - a->y*b->x);
    return 1;
}

static const luaL_Reg vec3_methods[] = {
    {"length",    vec3_length},
    {"normalize", vec3_normalize},
    {"dot",       vec3_dot},
    {"cross",     vec3_cross},
    {NULL, NULL}
};

// ============================================================================
// Vector2D
// ============================================================================

static int vec2_new(lua_State* L)
{
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    lua_push_vec2(L, x, y);
    return 1;
}

static int vec2_call(lua_State* L)
{
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    lua_push_vec2(L, x, y);
    return 1;
}

static int vec2_index(lua_State* L)
{
    lua_vec2_t* v = luaL_checkudata(L, 1, LUA_VEC2_MT);
    const char* k = luaL_checkstring(L, 2);
    if (k[1] == '\0') {
        if (k[0] == 'x') { lua_pushnumber(L, (lua_Number)v->x); return 1; }
        if (k[0] == 'y') { lua_pushnumber(L, (lua_Number)v->y); return 1; }
    }
    luaL_getmetatable(L, LUA_VEC2_MT);
    lua_getfield(L, -1, k);
    lua_remove(L, -2);
    return 1;
}

static int vec2_newindex(lua_State* L)
{
    lua_vec2_t* v = luaL_checkudata(L, 1, LUA_VEC2_MT);
    const char* k = luaL_checkstring(L, 2);
    float val     = (float)luaL_checknumber(L, 3);
    if (k[1] == '\0') {
        if (k[0] == 'x') { v->x = val; return 0; }
        if (k[0] == 'y') { v->y = val; return 0; }
    }
    return luaL_error(L, "Vector2D: no field '%s'", k);
}

static int vec2_tostring(lua_State* L)
{
    lua_vec2_t* v = luaL_checkudata(L, 1, LUA_VEC2_MT);
    lua_pushfstring(L, "Vector2D(%.4g, %.4g)", (double)v->x, (double)v->y);
    return 1;
}

static int vec2_add(lua_State* L)
{
    lua_vec2_t* a = lua_check_vec2(L, 1);
    lua_vec2_t* b = lua_check_vec2(L, 2);
    lua_push_vec2(L, a->x + b->x, a->y + b->y);
    return 1;
}

static int vec2_sub(lua_State* L)
{
    lua_vec2_t* a = lua_check_vec2(L, 1);
    lua_vec2_t* b = lua_check_vec2(L, 2);
    lua_push_vec2(L, a->x - b->x, a->y - b->y);
    return 1;
}

static int vec2_mul(lua_State* L)
{
    if (lua_type(L, 1) == LUA_TNUMBER) {
        float       s = (float)lua_tonumber(L, 1);
        lua_vec2_t* v = lua_check_vec2(L, 2);
        lua_push_vec2(L, v->x * s, v->y * s);
    } else {
        lua_vec2_t* v = lua_check_vec2(L, 1);
        float       s = (float)luaL_checknumber(L, 2);
        lua_push_vec2(L, v->x * s, v->y * s);
    }
    return 1;
}

static int vec2_unm(lua_State* L)
{
    lua_vec2_t* v = lua_check_vec2(L, 1);
    lua_push_vec2(L, -v->x, -v->y);
    return 1;
}

static int vec2_eq(lua_State* L)
{
    lua_vec2_t* a = lua_check_vec2(L, 1);
    lua_vec2_t* b = lua_check_vec2(L, 2);
    lua_pushboolean(L, a->x == b->x && a->y == b->y);
    return 1;
}

static int vec2_length(lua_State* L)
{
    lua_vec2_t* v = lua_check_vec2(L, 1);
    lua_pushnumber(L, (lua_Number)sqrtf(v->x*v->x + v->y*v->y));
    return 1;
}

static int vec2_normalize(lua_State* L)
{
    lua_vec2_t* v   = lua_check_vec2(L, 1);
    float       len = sqrtf(v->x*v->x + v->y*v->y);
    if (len < 1e-8f) {
        lua_push_vec2(L, 0.f, 0.f);
    } else {
        lua_push_vec2(L, v->x / len, v->y / len);
    }
    return 1;
}

static int vec2_dot(lua_State* L)
{
    lua_vec2_t* a = lua_check_vec2(L, 1);
    lua_vec2_t* b = lua_check_vec2(L, 2);
    lua_pushnumber(L, (lua_Number)(a->x*b->x + a->y*b->y));
    return 1;
}

static const luaL_Reg vec2_methods[] = {
    {"length",    vec2_length},
    {"normalize", vec2_normalize},
    {"dot",       vec2_dot},
    {NULL, NULL}
};

// ============================================================================
// Color
// ============================================================================

static int color_new(lua_State* L)
{
    lua_Integer r = luaL_checkinteger(L, 1);
    lua_Integer g = luaL_checkinteger(L, 2);
    lua_Integer b = luaL_checkinteger(L, 3);
    lua_push_color(L, (uint8_t)(r & 0xFF), (uint8_t)(g & 0xFF), (uint8_t)(b & 0xFF));
    return 1;
}

static int color_call(lua_State* L)
{
    lua_Integer r = luaL_checkinteger(L, 2);
    lua_Integer g = luaL_checkinteger(L, 3);
    lua_Integer b = luaL_checkinteger(L, 4);
    lua_push_color(L, (uint8_t)(r & 0xFF), (uint8_t)(g & 0xFF), (uint8_t)(b & 0xFF));
    return 1;
}

static int color_index(lua_State* L)
{
    lua_color_t* c = luaL_checkudata(L, 1, LUA_COLOR_MT);
    const char*  k = luaL_checkstring(L, 2);
    if (k[1] == '\0') {
        if (k[0] == 'r') { lua_pushinteger(L, (lua_Integer)c->r); return 1; }
        if (k[0] == 'g') { lua_pushinteger(L, (lua_Integer)c->g); return 1; }
        if (k[0] == 'b') { lua_pushinteger(L, (lua_Integer)c->b); return 1; }
    }
    luaL_getmetatable(L, LUA_COLOR_MT);
    lua_getfield(L, -1, k);
    lua_remove(L, -2);
    return 1;
}

static int color_newindex(lua_State* L)
{
    lua_color_t* c   = luaL_checkudata(L, 1, LUA_COLOR_MT);
    const char*  k   = luaL_checkstring(L, 2);
    lua_Integer  val = luaL_checkinteger(L, 3);
    if (k[1] == '\0') {
        if (k[0] == 'r') { c->r = (uint8_t)(val & 0xFF); return 0; }
        if (k[0] == 'g') { c->g = (uint8_t)(val & 0xFF); return 0; }
        if (k[0] == 'b') { c->b = (uint8_t)(val & 0xFF); return 0; }
    }
    return luaL_error(L, "Color: no field '%s'", k);
}

static int color_tostring(lua_State* L)
{
    lua_color_t* c = luaL_checkudata(L, 1, LUA_COLOR_MT);
    lua_pushfstring(L, "Color(%d, %d, %d)", (int)c->r, (int)c->g, (int)c->b);
    return 1;
}

static int color_eq(lua_State* L)
{
    lua_color_t* a = luaL_checkudata(L, 1, LUA_COLOR_MT);
    lua_color_t* b = luaL_checkudata(L, 2, LUA_COLOR_MT);
    lua_pushboolean(L, a->r == b->r && a->g == b->g && a->b == b->b);
    return 1;
}

// Color:pack() → integer 0xRRGGBB
static int color_pack(lua_State* L)
{
    lua_color_t* c = luaL_checkudata(L, 1, LUA_COLOR_MT);
    lua_pushinteger(L,
        ((lua_Integer)c->r << 16) |
        ((lua_Integer)c->g <<  8) |
         (lua_Integer)c->b);
    return 1;
}

// Color:unpack() → r, g, b
static int color_unpack(lua_State* L)
{
    lua_color_t* c = luaL_checkudata(L, 1, LUA_COLOR_MT);
    lua_pushinteger(L, (lua_Integer)c->r);
    lua_pushinteger(L, (lua_Integer)c->g);
    lua_pushinteger(L, (lua_Integer)c->b);
    return 3;
}

static const luaL_Reg color_methods[] = {
    {"pack",   color_pack},
    {"unpack", color_unpack},
    {NULL, NULL}
};

// ============================================================================
// Predefined color constants  (Color.RED, Color.WHITE, …)
// ============================================================================

typedef struct { const char* name; uint8_t r, g, b; } color_const_entry_t;

static const color_const_entry_t color_const_table[] = {
    { "RED",     255,   0,   0 },
    { "GREEN",     0, 255,   0 },
    { "BLUE",      0,   0, 255 },
    { "WHITE",   255, 255, 255 },
    { "BLACK",     0,   0,   0 },
    { "YELLOW",  255, 255,   0 },
    { "CYAN",      0, 255, 255 },
    { "MAGENTA", 255,   0, 255 },
    { "ORANGE",  255, 165,   0 },
    { "PURPLE",  128,   0, 128 },
    { NULL, 0, 0, 0 }
};

// __index on the Color constructor table — returns a fresh Color for named constants.
// Returning a new instance each time prevents callers from mutating a shared value.
static int color_const_index(lua_State* L)
{
    const char* k = lua_tostring(L, 2);
    if (k) {
        for (const color_const_entry_t* e = color_const_table; e->name; e++) {
            if (strcmp(k, e->name) == 0) {
                lua_push_color(L, e->r, e->g, e->b);
                return 1;
            }
        }
    }
    lua_pushnil(L);
    return 1;
}

// ============================================================================
// Registration helpers
// ============================================================================

// Build a metatable for a type: add methods, set all metamethods, pop it.
// Then create a global table with .new and __call → constructor.
static void register_type(lua_State* L,
                           const char*       mt_name,
                           const luaL_Reg*   methods,
                           lua_CFunction     index_fn,
                           lua_CFunction     newindex_fn,
                           lua_CFunction     tostring_fn,
                           lua_CFunction     add_fn,
                           lua_CFunction     sub_fn,
                           lua_CFunction     mul_fn,
                           lua_CFunction     unm_fn,
                           lua_CFunction     eq_fn,
                           lua_CFunction     new_fn,
                           lua_CFunction     call_fn,
                           const char*       global_name)
{
    // Build metatable
    luaL_newmetatable(L, mt_name);
    if (methods)     luaL_setfuncs(L, methods, 0);
    if (index_fn)    { lua_pushcfunction(L, index_fn);    lua_setfield(L, -2, "__index");    }
    if (newindex_fn) { lua_pushcfunction(L, newindex_fn); lua_setfield(L, -2, "__newindex"); }
    if (tostring_fn) { lua_pushcfunction(L, tostring_fn); lua_setfield(L, -2, "__tostring"); }
    if (add_fn)      { lua_pushcfunction(L, add_fn);      lua_setfield(L, -2, "__add");      }
    if (sub_fn)      { lua_pushcfunction(L, sub_fn);      lua_setfield(L, -2, "__sub");      }
    if (mul_fn)      { lua_pushcfunction(L, mul_fn);      lua_setfield(L, -2, "__mul");      }
    if (unm_fn)      { lua_pushcfunction(L, unm_fn);      lua_setfield(L, -2, "__unm");      }
    if (eq_fn)       { lua_pushcfunction(L, eq_fn);       lua_setfield(L, -2, "__eq");       }
    lua_pop(L, 1); // pop metatable

    // Global constructor table: MyType.new(…) and MyType(…)
    lua_newtable(L);
    if (new_fn) {
        lua_pushcfunction(L, new_fn);
        lua_setfield(L, -2, "new");
    }
    if (call_fn) {
        lua_newtable(L);                         // metatable for the constructor table
        lua_pushcfunction(L, call_fn);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);
    }
    lua_setglobal(L, global_name);
}

void lua_types_register(lua_State* L)
{
    register_type(L,
        LUA_VEC3_MT, vec3_methods,
        vec3_index, vec3_newindex, vec3_tostring,
        vec3_add, vec3_sub, vec3_mul, vec3_unm, vec3_eq,
        vec3_new, vec3_call, "Vector3D");

    register_type(L,
        LUA_VEC2_MT, vec2_methods,
        vec2_index, vec2_newindex, vec2_tostring,
        vec2_add, vec2_sub, vec2_mul, vec2_unm, vec2_eq,
        vec2_new, vec2_call, "Vector2D");

    register_type(L,
        LUA_COLOR_MT, color_methods,
        color_index, color_newindex, color_tostring,
        NULL, NULL, NULL, NULL, color_eq,
        color_new, color_call, "Color");

    // Extend the Color constructor table so that Color.RED, Color.WHITE, etc.
    // resolve to fresh Color instances via __index on its metatable.
    lua_getglobal(L, "Color");
    lua_getmetatable(L, -1);                  // metatable already has __call
    lua_pushcfunction(L, color_const_index);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 2);                            // pop metatable, Color table
}
