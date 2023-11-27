#pragma once
#include "Helpers.h"
#include "DataTypes.h"
#include "FuncBinding.h"
#include "GenericFuncBinding.h"
#include "BasicTraits.h"
#include "LuaRepresObjects.h"

namespace LuaStrap {
	void publishLuaStrapUtils(lua_State* ls);
	void publishStl(lua_State* ls);
}

