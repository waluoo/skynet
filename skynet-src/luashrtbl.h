#ifndef LUA_SHORT_STRING_TABLE_H
#define LUA_SHORT_STRING_TABLE_H

#ifndef DISABLE_SHORT_STRING

#include "lshrtbl.h"

#else

static inline int luaS_shrinfo(lua_State *L) { return 0; }
static inline int luaS_shrgen() { return 0; }
static inline void luaS_collectshr(int gen) {}
static inline void luaS_initshr() {}
static inline void luaS_exitshr() {}
static inline void luaS_initshrversion(lua_State *L) {}

#endif

#endif
