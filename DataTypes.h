#pragma once
#include "CppLuaInterface.h"
#include "Helpers.h"
#include <map>

namespace LuaStrap {

	// The different types of data that can be passed into bound functions.
	// Conceptually, only LuaData and BakedData are relevant to the user.

	struct LuaData {
		lua_State* ls;
		int idx;

		template <typename T>
		auto readAs() const -> PotentialOwner<T>;	// [-0, +n, m]
		void toLuaData() const;						// [-0, +1, e]
		void toBakedData() const;					// [-0, +1, m]
	};
	struct PendingData {
		lua_State* ls;
		int idx;

		template <typename T>
		auto readAs() const -> PotentialOwner<T>;	// [-0, +n, m]
		void toLuaData() const;						// [-0, +1, m]
		void toBakedData() const;					// [-0, +1, e]
		
		static void metatable(lua_State* ls);		// [-0, +1, m]
	};
	struct IndirectData {
		lua_State* ls;
		int idx;

		template <typename T>
		auto readAs() const -> PotentialOwner<T>;	// [-0, +n, m]
		void toLuaData() const;						// [-0, +1, m]
		void toBakedData() const;					// [-0, +1, e]

		static void metatable(lua_State* ls);		// [-0, +1, m]
	};
	struct BakedData {
		lua_State* ls;
		int idx;

		template <typename T>
		auto readAs() const -> PotentialOwner<T>;	// [-0, +n, m]
		void toLuaData() const;						// [-0, +1, m]
		void toBakedData() const;					// [-0, +1, e]

		template <typename T>
		static void metatable(lua_State* ls);		// [-0, +1, m]
	};
	struct FailData {
		template <typename T>
		auto readAs() const -> PotentialOwner<T>;	// [n/a]
		void toLuaData() const;						// [n/a]
		void toBakedData() const;					// [n/a]
	};

	using AnyDataVar = std::variant<FailData, LuaData, PendingData, IndirectData, BakedData>;
	struct AnyData : AnyDataVar {
		using AnyDataVar::variant;

		template <typename T>
		auto readAs() const {		// [-0, +n, m]
			return std::visit([](const auto& data) { return data.template readAs<T>(); }, *this);
		}
		void toLuaData() const {	// [-0, +1, e]
			return std::visit([](const auto& data) { data.toLuaData(); }, *this);
		}
		void toBakedData() const {	// [-0, +1, e]
			return std::visit([](const auto& data) { data.toBakedData(); }, *this);
		}
	};
	auto dataDispatch(lua_State* ls, int idx) -> AnyData;

	struct{} bakedReturnValueTag;
		// ^ this is what a bound func shall return if it directly puts its result on the lua stack,
		// instead of returning a c++ value to be translated into lua

	template <typename T, typename... Args>
	auto makeBakedData(const Args&... args, lua_State* ls) {	// [-0, +1, m]
		lua_checkstack(ls, 2);
		auto* dest = lua_newuserdata(ls, sizeof(T));
		auto* obj = new (dest) T{ args... };
		BakedData::metatable<T>(ls);
		lua_setmetatable(ls, -2);
		return bakedReturnValueTag;
	};

// DEFINITIONS

	// Forward decls
	template <typename Ret, typename... Args>
	void pushFunc(lua_State* ls, Ret(*f)(Args...));
	template <typename Ret, typename Class, typename... Args>
	void pushFunc(lua_State* ls, Ret(Class::* f)(Args...));
	template <typename Ret, typename Class, typename... Args>
	void pushFunc(lua_State* ls, Ret(Class::* f)(Args...) const);


	template <typename T>
	void BakedData::metatable(lua_State* ls) {	// [-0, +1]
		static auto refsPerLs = std::map<lua_State*, int>{};
		auto mainThread = getMainThread(ls);
		lua_checkstack(ls, 2);

		if (auto ref = refsPerLs.find(mainThread); ref == refsPerLs.end()) {
			lua_createtable(ls, 0, 0); {
				lua_pushvalue(ls, -1);
				lua_setfield(ls, -2, "__index");

				lua_pushcfunction(ls, [](lua_State* ls) {
					// (userdatum)
					if (lua_isuserdata(ls, 1)) {
						static_cast<T*>(lua_touserdata(ls, 1))->~T();
					}
					return 0;
				});
				lua_setfield(ls, -2, "__gc");

				if constexpr (LuaWritable<T>) {
					lua_pushcfunction(ls, [](lua_State* ls) {
						// (userdata of type T)
						auto hasMt = lua_getmetatable(ls, 1);
						if (!hasMt) {
							return luaL_error(ls, "Wrong argument for 'toLuaData'. Note: functions 'toLuaData' and 'toBakedData' of a baked object's metatable are meant for internal use. Use the library provided functions 'unbaked' and 'markedForBaking' instead.");
						}
						BakedData::metatable<T>(ls);
						if (!lua_rawequal(ls, -2, -1)) {
							return luaL_error(ls, "Wrong argument for 'toLuaData'. Note: functions 'toLuaData' and 'toBakedData' of a baked object's metatable are meant for internal use. Use the library provided functions 'unbaked' and 'markedForBaking' instead.");
						}

						auto* val = static_cast<T*>(lua_touserdata(ls, 1));
						LuaStrap::write(ls, *val);
						return 1;
					});
					lua_setfield(ls, -2, "toLuaData");
				}

				if constexpr (LuaInterfacable<T>) {
					lua_pushcfunction(ls, [](lua_State* ls) {
						// (luarepres of type T)
						auto* dest = lua_newuserdata(ls, sizeof(T));
						new (dest) T{ LuaStrap::unconditionalRead<T>(ls, 1) };
						BakedData::metatable<T>(ls);
						lua_setmetatable(ls, -2);
						return 1;
					});
					lua_setfield(ls, -2, "toBakedData");
				}

				if constexpr (requires{ LuaStrap::Traits<T>::members; }) {
					auto helper = [&](auto member) {
						if constexpr (std::is_member_function_pointer_v<decltype(member.second)>) {
							pushFunc(ls, member.second);
							lua_setfield(ls, -2, member.first);
						}
					};
					std::apply(
						[&]<typename... Ts>(Ts... ts) { int dummy[] = { (helper(ts), 0)... }; },
						LuaStrap::Traits<T>::members
					);
				}
			}
			lua_pushvalue(ls, -1);
			refsPerLs.emplace(mainThread, luaL_ref(ls, LUA_REGISTRYINDEX));
		}
		else {
			lua_rawgeti(ls, LUA_REGISTRYINDEX, ref->second);
		}
	}
	template <typename T>
	auto bakePendingData(lua_State* ls) -> T* {	// [-0, +n], -1 = pendingData
		// Turns pending data into indirect data
		lua_checkstack(ls, 1);
		void* pdata = lua_touserdata(ls, -1);
		auto luaDataStackObj = *static_cast<int*>(pdata);
		lua_rawgeti(ls, LUA_REGISTRYINDEX, luaDataStackObj);

		// -2 = pendingData, -1 = corresponding luaData
		auto pendingDataIdx = lua_gettop(ls) - 1;
		auto readAttempt = dataDispatch(ls, -1).readAs<T>();

		if (readAttempt) {
			lua_checkstack(ls, 2);

			// Overwrite the data referred to by "ref" with the baked data
			auto* dest = lua_newuserdata(ls, sizeof(T));
			auto* udata = new (dest) T{ std::move(*readAttempt) }; {
				BakedData::metatable<T>(ls);
				lua_setmetatable(ls, -2);
			}
			lua_rawseti(ls, LUA_REGISTRYINDEX, luaDataStackObj);

			// Mark subject as indirect userdata
			IndirectData::metatable(ls);
			lua_setmetatable(ls, pendingDataIdx);

			return udata;
		}
		else {
			return nullptr;
		}
	}

	template <typename T>
	auto LuaData::readAs() const -> PotentialOwner<T> {
		if constexpr (LuaInterfacable<T>) {
			auto val = LuaStrap::read<T>(ls, idx);
			if (val) {
				return std::move(*val);
			}
		}

		return std::monostate{};
	}
	template <typename T>
	auto PendingData::readAs() const -> PotentialOwner<T> {
		lua_checkstack(ls, 1);
		lua_pushvalue(ls, idx);

		auto pendingDataIdx = lua_gettop(ls);
		auto* bakedData = bakePendingData<T>(ls);
		popIfOnTop(ls, pendingDataIdx);

		if (bakedData) {
			return bakedData;
		}
		else {
			return std::monostate{};
		}
	}
	template <typename T>
	auto IndirectData::readAs() const -> PotentialOwner<T> {
		lua_checkstack(ls, 1);
		auto luaStackObj = *static_cast<int*>(lua_touserdata(ls, idx));
		lua_rawgeti(ls, LUA_REGISTRYINDEX, luaStackObj);

		auto referencedDataIdx = lua_gettop(ls);
		auto readAttempt = dataDispatch(ls, -1).readAs<T>();
		popIfOnTop(ls, referencedDataIdx);

		return readAttempt;
	}
	template <typename T>
	auto BakedData::readAs() const -> PotentialOwner<T> {
		lua_checkstack(ls, 2);
		BakedData::metatable<T>(ls);
		auto hasMt = lua_getmetatable(ls, idx);
		assert(hasMt && lua_rawequal(ls, -2, -1));
		lua_pop(ls, 2);
		return static_cast<T*>(lua_touserdata(ls, idx));
	}
	template <typename T>
	auto FailData::readAs() const -> PotentialOwner<T> {
		assert(false);
		return std::monostate{};
	}
}
