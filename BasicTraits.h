#pragma once
#include "CppLuaInterface.h"
#include "DataTypes.h"
#include <complex>
#include <vector>
#include <map>
#include <functional>

namespace LuaStrap {
	template <typename T, typename MemMap>
	auto aggregateRead(lua_State* ls, int idx, MemMap members) -> std::optional<T> {	// [-0, +0, m]
		idx = lua_absindex(ls, idx);

		if (lua_type(ls, idx) != LUA_TTABLE) {
			return std::nullopt;
		}
		auto res = std::optional<T>{ T{} };

		auto readMember = [&]<int index> {
			if constexpr (!std::is_member_function_pointer_v<std::tuple_element_t<1, std::tuple_element_t<index, MemMap>>>)
			{
				lua_checkstack(ls, 2);
				const auto& [memberName, memberPtr] = get<index>(members);
				LuaStrap::write(ls, memberName);
				lua_gettable(ls, idx);
				auto val = LuaStrap::readNoPush<std::decay_t<decltype(std::invoke(memberPtr, *res))>>(ls, -1);
				lua_pop(ls, 1);
				if (!val) {
					return false;
				}
				std::invoke(memberPtr, *res) = std::move(*val);
			}
			return true;
		};

		auto readAllMembers = [&] <int... indices>(std::integer_sequence<int, indices...>) -> std::optional<T> {
			if ((readMember.operator()<indices>() && ...)) {
				return res;
			}
			else {
				return std::nullopt;
			}
		};

		return readAllMembers(std::make_integer_sequence<int, std::tuple_size_v<MemMap>>{});
	}
	template <typename T, typename MemMap>
	void aggregateEmplace(lua_State* ls, const T& v, int idx, MemMap members) {	// [-0, +0, m]
		lua_checkstack(ls, 2);
		idx = lua_absindex(ls, idx);

		LuaStrap::BakedData::template metatable<T>(ls);
		lua_setmetatable(ls, idx);

		auto writeMember = [&](const auto& member) {
			if constexpr (!std::is_member_function_pointer_v<decltype(member.second)>) {
				LuaStrap::write(ls, member.first);
				LuaStrap::write(ls, std::invoke(member.second, v));
				lua_settable(ls, idx);
			}
		};

		if constexpr (std::tuple_size_v<MemMap> > 0) {
			std::apply([&]<typename... Members>(const Members&... members) {
				int dummy[] = { (writeMember(members), 0)... };
			}, members);
		}
	}
	template <typename T, typename MemMap>
	void aggregateWrite(lua_State* ls, const T& v, MemMap members) {	// [-0, +1, m]
		lua_createtable(ls, 0, 0);
		aggregateEmplace(ls, v, -1, members);
	}

	// Meant to be inherited from by a class specifying a memMap
	template <typename T>
	struct AggregateTraits {
		static auto read(lua_State* ls, int idx) -> std::optional<T> {
			return LuaStrap::aggregateRead<T>(ls, idx, LuaStrap::Traits<T>::members);
		}
		static void write(lua_State* ls, const T& v) {
			return LuaStrap::aggregateWrite<T>(ls, v, LuaStrap::Traits<T>::members);
		}
		static void emplace(lua_State* ls, const T& v, int idx) {
			return LuaStrap::aggregateEmplace<T>(ls, v, idx, LuaStrap::Traits<T>::members);
		}
	};	

	// ~~~ Traits for vocabulary types ~~~

	template <std::integral Int>
	struct Traits<Int> {
		static auto read(lua_State* ls, int idx) {
			return lua_isinteger(ls, idx) ?
				std::optional<Int>{ lua_tointeger(ls, idx) } :
				std::nullopt;
		}
		static void write(lua_State* ls, const Int& v) { lua_pushinteger(ls, v); }
	};
	template <std::floating_point Float>
	struct Traits<Float> {
		static auto read(lua_State* ls, int idx) {
			return lua_isnumber(ls, idx) ?
				std::optional<Float>{ lua_tonumber(ls, idx) } :
				std::nullopt;
		}
		static void write(lua_State* ls, const Float& v) { lua_pushnumber(ls, v); }
	};
	template <>
	struct Traits<bool> {
		static auto read(lua_State* ls, int idx) {
			return lua_isboolean(ls, idx) ?
				std::optional<bool>{ lua_toboolean(ls, idx) } :
				std::nullopt;
		}
		static void write(lua_State* ls, const bool& v) { lua_pushboolean(ls, v); }
	};
	template <>
	struct Traits<std::string> {
		static auto read(lua_State* ls, int idx) {
			return lua_type(ls, idx) == LUA_TSTRING ?
				std::optional<std::string>{lua_tostring(ls, idx)} :
				std::nullopt;
		}
		static void write(lua_State* ls, const std::string& v) { lua_pushstring(ls, v.c_str()); }
	};
	template <>
	struct Traits<const char*> {
		static void write(lua_State* ls, const char* v) { lua_pushstring(ls, v); }
	};
	template <typename T>
	struct Traits<std::complex<T>> {
		// { [1] = real, [2] = imag }
		static auto read(lua_State* ls, int idx) -> std::optional<std::complex<T>> {	// [-0, +0]
			if (lua_type(ls, idx) != LUA_TTABLE) {
				return std::nullopt;
			}

			lua_checkstack(ls, 2);
			lua_geti(ls, idx, 1);
			lua_geti(ls, idx, 2);
			if (!lua_isnumber(ls, -2) || !lua_isnumber(ls, -1)) {
				return std::nullopt;
			}

			auto r = lua_tonumber(ls, -2);
			auto i = lua_tonumber(ls, -1);
			lua_pop(ls, 2);

			return std::optional{ std::complex{r, i} };
		}
		static void emplace(lua_State* ls, const std::complex<T>& v, int idx) {
			LuaStrap::write(ls, v.real());
			lua_rawseti(ls, idx, 1);
			LuaStrap::write(ls, v.imag());
			lua_rawseti(ls, idx, 2);
		}
	};

	template <typename Val, size_t size>
	struct Traits<std::array<Val, size>> {
		// { [1] = val1, ... }
		static auto read(lua_State* ls, int idx) -> std::optional<std::array<Val, size>> {	// [-0, +0]		
			auto origTop = lua_gettop(ls);
			auto result = std::optional<std::array<Val, size>>{ std::in_place };
			auto didSucceed = (readArrayUpTo<Val>(ls, idx, size, result->begin()) == size);
			assert(lua_gettop(ls) == origTop);

			if (didSucceed) {
				return result;
			}
			else {
				return std::nullopt;
			}
		}
		static void emplace(lua_State* ls, const std::array<Val, size>& v, int idx) {
			for (std::size_t i = 0; i < v.size(); ++i) {
				LuaStrap::write(ls, v[i]);
				lua_rawseti(ls, idx, i + 1);
			}
		}
	};
	template <typename Val>
	struct Traits<std::vector<Val>> {
		// { [1] = val1, ... }
		static auto read(lua_State* ls, int idx) -> std::optional<std::vector<Val>> {	// [-0, +0]
			auto origTop = lua_gettop(ls);
			auto result = std::optional<std::vector<Val>>{ std::in_place };
			auto didSucceed = (readArrayUnlimited<Val>(ls, idx, back_inserter(*result)) != -1);
			assert(lua_gettop(ls) == origTop);

			if (didSucceed) {
				return result;
			}
			else {
				return std::nullopt;
			}
		}
		static void emplace(lua_State* ls, const std::vector<Val>& v, int idx) {
			for (std::size_t i = 0; i < v.size(); ++i) {
				LuaStrap::write(ls, v[i]);
				lua_seti(ls, idx, i + 1);
			}
		}
	};
	template <typename Key, typename Val>
	struct Traits<std::map<Key, Val>> {
		// { [key1] = val1, ... }
		static auto read(lua_State* ls, int idx) -> std::optional<std::map<Key, Val>> {	// [-0, +0]
			if (lua_type(ls, idx) != LUA_TTABLE) {
				return std::nullopt;
			}
			auto result = std::map<Key, Val>{};

			lua_pushnil(ls);
			while (lua_next(ls, idx)) {
				// stack: -2 = key, -1 = val
				auto key = LuaStrap::readNoPush<Key>(ls, -2);
				auto val = LuaStrap::readNoPush<Val>(ls, -1);
				lua_pop(ls, 1);

				if (!key || !val) {
					return std::nullopt;
				}

				auto [_, didInsert] = result.emplace(std::move(*key), std::move(*val));
				if (!didInsert) {
					// Two keys of a lua table got translated into an equivalent c++ key
					return std::nullopt;
				}
			}

			return std::optional{ result };
		}
		static void emplace(lua_State* ls, const std::map<Key, Val>& v, int absIdx) {
			clearTable(ls, absIdx);
			for (const auto& [key, val] : v) {
				LuaStrap::write(ls, key);
				LuaStrap::write(ls, val);
				lua_settable(ls, absIdx);
			}
		}
	};
	template <typename Val>
	struct Traits<std::optional<Val>> {
		// Val or nil
		static auto read(lua_State* ls, int idx) {		// [-0, +0]
			using Ret = std::optional<std::optional<Val>>;			

			if (lua_isnil(ls, idx)) {
				auto r = Ret{ std::in_place, std::optional<Val>{} };
				return r;
			}
			else {
				auto valOpt = LuaStrap::readNoPush<Val>(ls, idx);
				if (valOpt) {
					return Ret{ std::in_place, std::move(valOpt) };
				}
				else {
					return Ret{};
				}
			}
		}
		static void emplace(lua_State* ls, const std::optional<Val>& v, int idx) requires
			requires{ LuaStrap::emplace(ls, v, idx); }
		{
			if (v) {
				LuaStrap::emplace(ls, v, idx);
			}
			else {
				lua_pushnil(ls);
			}
		}
		static void write(lua_State* ls, const std::optional<Val>& v) {
			if (v) {
				LuaStrap::write(ls, v);
			}
			else {
				lua_pushnil(ls);
			}
		}
		static auto defaultValue(lua_State* ls) -> std::optional<Val> {
			return std::nullopt;
		}
	};
	template <typename... Alts>
	struct Traits<std::variant<Alts...>> {
		static auto read(lua_State* ls, int idx) -> std::optional<std::variant<Alts...>> {	// [-0, +0]
			auto res = std::optional<std::variant<Alts...>>{};
			auto readAlternative = [&]<typename Alt> {
				auto val = LuaStrap::readNoPush<Alt>(ls, idx);
				if (val) {
					res = val;
					return true;
				}
				else {
					return false;
				}
			};

			auto dummy = (readAlternative.operator()<Alts>() || ...);
			return res;
		}
		static void emplace(lua_State* ls, const std::variant<Alts...>& v, int idx) requires
			(requires{ LuaStrap::emplace(ls, std::declval<const Alts&>(), idx); } && ...)
		{
			visit([&]<typename Alt>(const Alt& alternative) {
				LuaStrap::emplace(ls, alternative, idx);
			}, v);
		}
		static void write(lua_State* ls, const std::variant<Alts...>& v) {
			visit([&]<typename Alt>(const Alt& alternative) {
				LuaStrap::write(ls, alternative);
			}, v);
		}
	};
	template <typename... Vals>
	struct Traits<std::tuple<Vals...>> {
		// { [1] = val1, ... }
		static auto read(lua_State* ls, int idx) -> std::optional<std::tuple<Vals...>> {	// [-0, +0]
			if (lua_type(ls, idx) != LUA_TTABLE) {
				return std::nullopt;
			}

			std::tuple<Vals...> res;

			auto readElem = [&]<lua_Integer i>() {
				lua_rawgeti(ls, idx, i + 1);
				auto val = LuaStrap::readNoPush<std::tuple_element_t<i, decltype(res)>>(ls, -1);
				lua_pop(ls, 1);
				if (val) {
					get<i>(res) = std::move(*val);
					return true;
				}
				else {
					return false;
				}
			};
			auto readAll = [&]<int... indices>(std::integer_sequence<int, indices...>) {
				auto success = (readElem.template operator()<indices>() && ...);
				return success ? std::optional{ res } : std::nullopt;
			};
			return readAll(std::make_integer_sequence<int, sizeof...(Vals)>{});
		}
		static void emplace(lua_State* ls, const std::tuple<Vals...>& v, int idx) {
			auto writeElem = [&]<lua_Integer i>() {
				LuaStrap::write(ls, get<i>(v));
				lua_seti(ls, idx, i + 1);
				return 0;
			};
			auto writeAll = [&]<int... indices>(std::integer_sequence<int, indices...>) {
				int dummy[] = { writeElem.template operator()<indices>()... };
			};
			writeAll(std::make_integer_sequence<int, sizeof...(Vals)>{});
		}
	};
	template <typename First, typename Second>
	struct Traits<std::pair<First, Second>> {
		// { [1] = val1, [2] = val2 }
		static auto read(lua_State* ls, int idx) -> std::optional<std::pair<First, Second>> {	// [-0, +0]
			if (lua_type(ls, idx) != LUA_TTABLE) {
				return std::nullopt;
			}

			lua_rawgeti(ls, idx, 1);
			auto first = LuaStrap::read<First>(ls, -1);
			lua_pop(ls, 1);
			if (!first) {
				return std::nullopt;
			}

			lua_rawgeti(ls, idx, 2);
			auto second = LuaStrap::read<Second>(ls, -1);
			lua_pop(ls, 1);
			if (!second) {
				return std::nullopt;
			}

			return std::optional{ std::pair{*first, *second} };
		}
		static void emplace(lua_State* ls, const std::pair<First, Second>& v, int idx) {
			LuaStrap::write(ls, v.first);
			lua_seti(ls, idx, 1);
			LuaStrap::write(ls, v.second);
			lua_seti(ls, idx, 2);
		}
	};

	template <>
	struct Traits<lua_State*> {
		// Useful trick:
		// If a c++ function (undergoing 'pushFunc') wants to know the lua state it is invoked in,
		// it simply needs a lua_State* parameter at the end of its parameter list, and the system will supply it.
		// This parameter shall then be ignored by the caller.

		static auto read(lua_State* ls, int idx) -> std::optional<lua_State*> {
			if (lua_isthread(ls, idx)) {
				return std::optional{ lua_tothread(ls, idx) };
			}
			else if (lua_isnil(ls, idx)) {
				return std::optional{ ls };
			}
			else {
				return std::nullopt;
			}
		}
		static void write(lua_State* ls, lua_State* v) {
			lua_checkstack(v, 1);
			lua_pushthread(v);
			if (ls != v) {
				assert(getMainThread(ls) == getMainThread(v));
				lua_xmove(v, ls, 1);
			}
		}
		static auto defaultValue(lua_State* ls) {
			return ls;
		}
	};
}
