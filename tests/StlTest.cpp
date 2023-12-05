#include "Tests.h"
#include "lua.hpp"
#include "lauxlib.h"
#include <iostream>

void doStlTest(lua_State* ls) {
	auto testFailed = luaL_dofile(ls, "StlTest.lua");
		// ^ replace with your own absolute path

	if (testFailed) {
		std::cout << "StlTest.cpp: " << lua_tostring(ls, -1) << "\n";
		lua_pop(ls, 1);
	}
}