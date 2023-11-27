#include "Tests.h"
#include "lua.hpp"
#include "lauxlib.h"

void doStlTest(lua_State* ls) {
	luaL_dofile(ls, "StlTest.lua");
}