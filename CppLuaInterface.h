#pragma once
#include "lua.hpp"
#include "lauxlib.h"
#include <optional>
#include <string>
#include <cassert>

namespace LuaStrap {
	template <typename T>
	struct Traits {
		// to be specialized for individual classes
	};

	// LuaWritable types can be used as return values from bound functions
	template <typename T>
	concept LuaWritable =
		std::same_as<T, std::decay_t<T>>
	&& (
		requires {
			{ LuaStrap::Traits<T>::write(std::declval<lua_State*>(), std::declval<const T&>()) };	// [-0, +1, m]
		} || requires {
			{ LuaStrap::Traits<T>::emplace(std::declval<lua_State*>(), std::declval<const T&>(), int{}) };	// [-0, +0, m]
		}
	);

	// LuaInterfacable types can be used as args to bound functions (by value or const ref), and can be freely baked and unbaked
	template <typename T>
	concept LuaInterfacable =
		LuaWritable<T> &&
		requires {
			{ LuaStrap::Traits<T>::read(std::declval<lua_State*>(), int{}) } -> std::same_as<std::optional<T>>;	// [-0, +0 or +n, m]
		};

	// LuaInterfacableWithDefault types can be omitted from the tail end of an invocation from lua
	template <typename T>
	concept LuaInterfacableWithDefault = LuaInterfacable<T> && requires {
		{ LuaStrap::Traits<T>::defaultValue((lua_State*)nullptr) } -> std::convertible_to<T>;
	};

	// Rules for writing traits
	// - 'read' shall return an optional<T>, which is empty in case of failure.
	// - 'emplace' shall be defined for types whose lua representation has object semantics (i.e. a table). It represents
	//	the act of overwriting, and only types with 'emplace' defined can be passed to bound funcs by mutable reference.
	// - None of these functions shall signal a lua error, since that would do a longjmp and possibly lead to UB.
	// - 'write'/'emplace' are free to assume at least 1 free stack space. 'read' may not assume any free space.
	// - 'read' should generally not push anything on stack, but some special types require it (see LuaRepresObjects.h).
	//	 Doing so is allowed, but results in the type only being usable in a few cases.
	//	 Also, they must be destructible even after their stack space was deleted.
	// - All indices passed into trait functions are assumed to be absolute.



	template <LuaInterfacable T>
	auto read(lua_State* ls, int idx) -> std::optional<T>
	{
		idx = lua_absindex(ls, idx);
		using Tr = LuaStrap::Traits<T>;

		return Tr::read(ls, idx);
	}
	template <LuaWritable T>
	void write(lua_State* ls, const T& t)
	{
		using Tr = LuaStrap::Traits<T>;

		if constexpr (requires { Tr::write(ls, t); }) {
			lua_checkstack(ls, 1);
			Tr::write(ls, t);
		}
		else {
			lua_checkstack(ls, 2);
			lua_createtable(ls, 0, 0);
			Tr::emplace(ls, t, lua_gettop(ls));
		}

	}
	template <LuaWritable T>
	void emplace(lua_State* ls, const T& t, int idx) requires
		requires { LuaStrap::Traits<T>::emplace(ls, t, idx); }
	{
		idx = lua_absindex(ls, idx);
		lua_checkstack(ls, 1);
		LuaStrap::Traits<T>::emplace(ls, t, idx);
	}

	template <LuaInterfacable T>
	auto readNoPush(lua_State* ls, int idx) -> std::optional<T>
	{
		idx = lua_absindex(ls, idx);
		using Tr = LuaStrap::Traits<T>;

		auto origTop = lua_gettop(ls);
		auto res = Tr::read(ls, idx);
		assert(lua_gettop(ls) == origTop);
		return res;
	}
	template <LuaInterfacable T>
	auto unconditionalRead(lua_State* ls, int idx, std::string errorMessage = "") -> T
	{
		auto val = LuaStrap::read<T>(ls, idx);
		if (!val) {
			lua_checkstack(ls, 1);
			luaL_error(ls, errorMessage.c_str());
		}
		return *val;
	}

	template <typename T>
	auto sinkArrayElem(lua_State* ls, int tblIdx, int elmKey, std::invocable<T&&> auto&& sink) {	// [-0, +0 (unless sink pushes), m]
		tblIdx = lua_absindex(ls, tblIdx);
		lua_checkstack(ls, 1);

		lua_geti(ls, tblIdx, elmKey);
		if (lua_isnil(ls, -1)) {
			lua_pop(ls, 1);
			return 0;
		}
		auto val = LuaStrap::readNoPush<T>(ls, -1);
		lua_pop(ls, 1);
		if (!val) {
			return -1;
		}
		sink(std::move(*val));
		return 1;
	}

	// For a lua array at idx, successively reads each of the elems and sinks them.
	// Returns how many elems were read/sunk, -1 in case of wrong format.
	template <typename T>
	auto readArrayUnlimited(lua_State* ls, int idx, std::invocable<T&&> auto&& sink) {
		idx = lua_absindex(ls, idx);

		if (lua_type(ls, idx) != LUA_TTABLE) {
			return -1;
		}

		for (auto i = 0;; ++i) {
			auto sinkRes = sinkArrayElem<T>(ls, idx, i + 1, sink);
			if (sinkRes == 1)		{}
			else if (sinkRes == 0)	{ return i; }
			else /*sinkRes == -1*/	{ return -1; }
		}
	}
	template <typename T>
	auto readArrayUnlimited(lua_State* ls, int idx, std::output_iterator<T> auto&& dest) {
		return readArrayUnlimited<T>(ls, idx, outputItToSinkFunc<T>(dest));
	}

	// Like 'readArrayUnlimited', but the array must have at most 'capacity' elems
	template <typename T>
	auto readArrayUpTo(lua_State* ls, int idx, int capacity, std::invocable<T&&> auto&& sink) {
		idx = lua_absindex(ls, idx);

		if (lua_type(ls, idx) != LUA_TTABLE) {
			return -1;
		}

		lua_geti(ls, idx, capacity + 1);
		if (!lua_isnil(ls, -1)) {
			return -1;
		}
		lua_pop(ls, 1);
				
		for (auto i = 0; i < capacity; ++i) {
			auto sinkRes = sinkArrayElem<T>(ls, idx, i + 1, sink);
			if (sinkRes == 1)		{}
			else if (sinkRes == 0)	{ return i; }
			else /*sinkRes == -1*/	{ return -1; }
		}
		return capacity;
	}
	template <typename T>
	auto readArrayUpTo(lua_State* ls, int idx, int capacity, std::output_iterator<T> auto&& dest) {
		return readArrayUpTo<T>(ls, idx, capacity, [dest](auto&& val) mutable {
			*dest = std::forward<decltype(val)>(val);
			++dest;
		});
	}

	// For a bound function with parameters Args..., returns the min and max amount of arguments (inclusively)
	// it can be called with - based on how many args from the right have default values.
	template <typename... Args>
	constexpr auto getMinMaxArgumentCount() {
		auto minArgCount = 0;
		constexpr auto maxArgCount = sizeof...(Args);
		if constexpr (maxArgCount > 0) {
			auto argNum = 1;
			int dummy[] = { (
				(minArgCount = (LuaInterfacableWithDefault<Args> ? minArgCount : argNum)),
				++argNum
			)... };
		}

		return std::pair{ minArgCount, maxArgCount };
	}
}
