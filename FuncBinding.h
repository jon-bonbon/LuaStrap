#pragma once
#include "DataTypes.h"
#include <set>
#include <array>
#include <functional>

namespace LuaStrap {
	struct TryToCallResult {
		size_t howManyArgsRead;
		size_t howManyArgsTotal;
		std::string errMsg = "";
	};

	// Calls a c++ invocable (of format [-0, +n, m]), assuming the arguments are represented at lua stack positions 1 to n.
	// The arguments can be in any format (see DataTypes.h).
	// In case of failure, returns an error message (empty string in case of success).
	template <typename Invoc, typename Ret, typename... Args>
		requires std::invocable<Invoc, Args...>
	auto tryToCallRaw(lua_State* ls, Invoc f) -> TryToCallResult {		// [-0, +n, m]
		auto [minArgCount, maxArgCount] = getMinMaxArgumentCount<Args...>();
		auto origTop = lua_gettop(ls);
		if (!(origTop >= minArgCount && origTop <= maxArgCount)) {
			return { 0, sizeof...(Args), wrongArgumentCountError(minArgCount, maxArgCount, origTop) };
		}

		// Translate args from lua to c++
		auto translatedArgs = std::tuple<PotentialOwner<std::decay_t<Args>>...>{};
		auto translationRes = [&] <int... indices>(std::integer_sequence<int, indices...> intSeq) -> std::optional<TryToCallResult>
		{
			// Read the args from the left, [first-supplied-arg to last-supplied-arg]

			auto failedArgLuaIdx = 0;	// 0 means none failed
			auto step = [&]<int index, typename Arg> {
				if (index >= origTop){
					return false;
				}
				get<index>(translatedArgs) = dataDispatch(ls, index + 1).readAs<std::decay_t<Arg>>();
				if (!get<index>(translatedArgs)) {
					failedArgLuaIdx = index + 1;
					return false;
				}
				else {
					return true;
				}
			};
			auto dummy = (step.template operator()<indices, Args>() && ...);
			if (failedArgLuaIdx != 0) {
				lua_settop(ls, origTop);
				return TryToCallResult{
					size_t(failedArgLuaIdx) - 1, sizeof...(Args), std::string{ "Failed reading argument #" } + std::to_string(failedArgLuaIdx) + "."
				};
			}

			// Fill args with default values from the right, [last-desired-arg to last-supplied-arg)

			[&] <int... reverseIndices>(std::integer_sequence<int, reverseIndices...>) {
				auto step = [&]<int reverseIndex>{
					using Arg = std::tuple_element_t<reverseIndex, std::tuple<Args...>>;
					if constexpr (LuaInterfacableWithDefault<Arg>) {
						auto success = get<reverseIndex>(translatedArgs) = LuaStrap::Traits<Arg>::defaultValue(ls);
						assert(success);
					}
				};
				auto dummy = ((
					step.template operator()<reverseIndices>(),
					reverseIndices >= origTop
				) && ...);
			}(reverseIotaSequence(intSeq));

			return std::nullopt;
		}(std::make_integer_sequence<int, sizeof...(Args)>{});
		if (translationRes) {
			return *translationRes;
		}

		// Invoke the invocable
		if constexpr (std::is_same_v<Ret, void> || std::is_same_v<Ret, decltype(bakedReturnValueTag)>) {
			std::apply(
				[&](PotentialOwner<std::decay_t<Args>>&... arg) { std::invoke(f, *arg...); },
				translatedArgs
			);
		}
		else {
			lua_checkstack(ls, 1);
			LuaStrap::write(ls, std::apply(
				[&](PotentialOwner<std::decay_t<Args>>&... arg) { return std::invoke(f, *arg...); },
				translatedArgs
			));
		}

		// For arguments taken by mutable reference, and passed in as lua data, emplace their new value into their lua representation
		auto res = TryToCallResult{ sizeof...(Args), sizeof...(Args), "" };
		auto emplaceArg = [&]<int index> {
			using ArgType = std::tuple_element_t<index, std::tuple<Args...>>;
			if constexpr (std::is_lvalue_reference_v<ArgType> && !std::is_const_v<std::remove_reference_t<ArgType>>) {
				if (auto* val = get_if<1>(&get<index>(translatedArgs))) {
					if constexpr (requires{ LuaStrap::emplace(ls, *val, index + 1); }) {
						LuaStrap::emplace(ls, *val, index + 1);
						return true;
					}
					else {
						res = { sizeof...(Args), sizeof...(Args), failedToEmplaceError(typeid(ArgType).name()) };
						return false;
					}
				}
			}
			return true;
		};
		auto emplaceArgs = [&]<int... indices>(std::integer_sequence<int, indices...>) {
			auto dummy = (emplaceArg.template operator()<indices>() && ...);
		};
		emplaceArgs(std::make_integer_sequence<int, sizeof...(Args)>{});

		return res;
	}
	template <typename Ret, typename... Args>
	auto tryToCall(lua_State* ls, Ret(*f)(Args...)) {
		return tryToCallRaw<Ret(*)(Args...), Ret, Args...>(ls, f);
	}
	template <typename Ret, typename Class, typename... Args>
	auto tryToCall(lua_State* ls, Ret(Class::*f)(Args...)) {
		return tryToCallRaw<Ret(Class::*)(Args...), Ret, Class&, Args...>(ls, f);
	}
	template <typename Ret, typename Class, typename... Args>
	auto tryToCall(lua_State* ls, Ret(Class::* f)(Args...) const) {
		return tryToCallRaw<Ret(Class::*)(Args...) const, Ret, const Class&, Args...>(ls, f);
	}
	struct TryToCallSuccessivelyResult {
		bool didAnySucceed = false;

		// info about the most recently attempted overload
		bool doesReturnAnything = false;
		TryToCallResult mostRecentAttempt;
	};
	template <typename... Fs>
	auto tryToCallSuccessively(lua_State* ls, Fs... fs) -> TryToCallSuccessivelyResult {
		auto res = TryToCallSuccessivelyResult{};
		res.didAnySucceed = ((
			res.doesReturnAnything = returnsAnything(fs),
			res.mostRecentAttempt = tryToCall(ls, fs),
			res.mostRecentAttempt.errMsg == ""
		) || ...);

		return res;
	}

	// Wraps a c++ invocable (of format [-0, +n, m]) such that it can be called from lua, and pushes it on stack top.
	template <typename Invoc, typename Ret, typename... Args>
		requires std::invocable<Invoc, Args...>
	void pushFuncRaw(lua_State* ls, Invoc f) {	// [-0, +1, m]
		lua_checkstack(ls, 2);
		new (lua_newuserdata(ls, sizeof(Invoc))) Invoc{ f };
		lua_pushcclosure(ls, [](lua_State* ls) {
			auto invoc = *(Invoc*)lua_touserdata(ls, lua_upvalueindex(1));
			auto callRes = tryToCallRaw<Invoc, Ret, Args...>(ls, invoc);

			if (callRes.errMsg == "") {
				return int{ !std::same_as<Ret, void> };
			}
			else {
				lua_checkstack(ls, 1);
				return luaL_error(ls, callRes.errMsg.c_str());
			}

			// Reading the args may have left something on the lua stack.
			// That will be automatically cleaned now (the lua function is ending).
		}, 1);
	}

	template <typename Ret, typename... Args>
	void pushFunc(lua_State* ls, Ret(*f)(Args...)) {
		pushFuncRaw<decltype(f), Ret, Args...>(ls, f);
	}
	template <typename Ret, typename Class, typename... Args>
	void pushFunc(lua_State* ls, Ret(Class::*f)(Args...)) {
		pushFuncRaw<decltype(f), Ret, Class&, Args...>(ls, f);
	}
	template <typename Ret, typename Class, typename... Args>
	void pushFunc(lua_State* ls, Ret(Class::* f)(Args...) const) {
		pushFuncRaw<decltype(f), Ret, const Class&, Args...>(ls, f);
	}

	template <typename... FuncPtrs>
	void pushOverloadedFunc(lua_State* ls, FuncPtrs... fs) {		// [-0, +1, m]
		using FuncPtrPack = std::tuple<FuncPtrs...>;
		new (lua_newuserdata(ls, sizeof(FuncPtrPack))) FuncPtrPack{ fs... };
		lua_pushcclosure(ls, [](lua_State* ls) {
			auto fs = *(FuncPtrPack*)lua_touserdata(ls, lua_upvalueindex(1));

			auto callResult = std::apply(
				[ls]<typename... Fs>(Fs... fs) { return tryToCallSuccessively(ls, fs...); },
				fs
			);

			if (callResult.didAnySucceed) {
				return int{ callResult.doesReturnAnything };
			}
			else {
				lua_checkstack(ls, 1);
				return luaL_error(ls, "None of the overloads are compatible with the given arguments.");
			}

			// Reading the args may have left something on the lua stack.
			// That will be automatically cleaned now (the lua function is ending).
		}, 1);
	}
}
