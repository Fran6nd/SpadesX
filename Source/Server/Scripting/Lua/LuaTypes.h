// LuaTypes.h — Vector2D, Vector3D, Color Lua userdata types.
//
// All three are lightweight value types backed by full userdata so the GC
// manages their memory.  Each has a metatable registered under a unique
// name; use lua_check_*/lua_test_* to retrieve them safely from the stack.
//
// Globals exposed to scripts:
//   Vector3D(x,y,z)  or  Vector3D.new(x,y,z)
//   Vector2D(x,y)    or  Vector2D.new(x,y)
//   Color(r,g,b)     or  Color.new(r,g,b)

#ifndef LUA_TYPES_H
#define LUA_TYPES_H

#include <lua.h>
#include <stdint.h>

// Metatable registry names
#define LUA_VEC2_MT  "SpadesX.Vector2D"
#define LUA_VEC3_MT  "SpadesX.Vector3D"
#define LUA_COLOR_MT "SpadesX.Color"

// C representations — same layout as the userdata payload
typedef struct { float   x, y;    } lua_vec2_t;
typedef struct { float   x, y, z; } lua_vec3_t;
typedef struct { uint8_t r, g, b; } lua_color_t;

// Register all type metatables and global constructors into L.
// Must be called once per Lua state before loading any scripts.
void lua_types_register(lua_State* L);

// ---- Push helpers: allocate new userdata, set metatable, return pointer ----
lua_vec2_t*  lua_push_vec2(lua_State* L, float x, float y);
lua_vec3_t*  lua_push_vec3(lua_State* L, float x, float y, float z);
lua_color_t* lua_push_color(lua_State* L, uint8_t r, uint8_t g, uint8_t b);
lua_color_t* lua_push_color_u32(lua_State* L, uint32_t c); // c = 0x00RRGGBB

// ---- Check helpers: raise a Lua error if the value at idx is wrong type ----
lua_vec2_t*  lua_check_vec2(lua_State* L, int idx);
lua_vec3_t*  lua_check_vec3(lua_State* L, int idx);
lua_color_t* lua_check_color(lua_State* L, int idx);

// ---- Test helpers: return NULL silently if wrong type (no error raised) ----
lua_vec2_t*  lua_test_vec2(lua_State* L, int idx);
lua_vec3_t*  lua_test_vec3(lua_State* L, int idx);
lua_color_t* lua_test_color(lua_State* L, int idx);

#endif // LUA_TYPES_H
