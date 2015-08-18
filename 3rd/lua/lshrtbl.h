#ifndef share_short_string_h
#define share_short_string_h

#include "lobject.h"

LUA_API void luaS_initshr();
LUA_API void luaS_exitshr();
LUAI_FUNC TString *luaS_newshr(lua_State *L, const char *str, lu_byte l);
LUAI_FUNC TString *luaS_markshr(TString *);	// for gc mark
LUA_API int luaS_shrgen();
LUA_API int luaS_collectshr(int gen);
LUA_API int luaS_shrinfo(lua_State *L);
LUA_API void luaS_initshrversion(lua_State *L);	// for clone

#endif
