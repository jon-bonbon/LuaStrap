#pragma once
#include "lua.hpp"
#include "lauxlib.h"
#include <span>
#include <variant>
#include <memory>
#include <type_traits>
#include <cassert>
#include <functional>
#include <string>
#include <algorithm>

namespace LuaStrap {

	// Optionally either contains a T object, or simply refers to it. Dereference to access the object.
	template <typename T>
	class PotentialOwner : public std::variant<std::monostate, T, T*> {
		using Variant = std::variant<std::monostate, T, T*>;
	public:
		static_assert(std::same_as<T, std::decay_t<T>>);

		using Variant::variant;
		PotentialOwner(const PotentialOwner&) = default;
		PotentialOwner(PotentialOwner&&) = default;

		auto operator=(const PotentialOwner&) -> PotentialOwner& = default;
		auto operator=(PotentialOwner&&) -> PotentialOwner& = default;

		auto operator*() -> T& {
			auto alt = Variant::index();
			switch (alt) {
				case 1: return get<1>(*this); break;
				case 2: return *get<2>(*this); break;
				default: assert(false); break;
			}
		}
		auto operator*() const -> const T& {
			auto alt = Variant::index();
			switch (alt) {
				case 1: return get<1>(*this); break;
				case 2: return *get<2>(*this); break;
				default: assert(false); break;
			}
		}
		operator bool() const {
			return !holds_alternative<std::monostate>(*this);
		}
	};

	// ~ Size and memory ~
	
	// Rounds the value of 'a' to the nearest multiple of 'b' not-less than 'a'
	template <typename I>
	constexpr auto integerCeil(I a, I b) {
		return a * (a / b + I(a % b != 0));
	}

	// Computes the smallest size a memory arena must be to ensure that a T object fits inside,
	// given both the object's and the arena's alignment requirements
	template <typename T, size_t arenaAlignment = 1>
	constexpr auto minNeededSpace = sizeof(T) + std::max((std::size_t)0, alignof(T) - arenaAlignment);

	template <typename... Ts>
	constexpr auto largestTupleElem(std::tuple<Ts...>* = nullptr) {
		return (std::max({ sizeof(Ts)... }));
	}

	template <size_t arenaAlignment = 1, typename... Ts>
	constexpr auto mostSpaciousTupleElem(std::tuple<Ts...>* = nullptr) {
		return (std::max({ minNeededSpace<Ts, arenaAlignment>... }));
	}

	// An on-stack memory pool. Elements are inserted sequentially, and their addresses are remembered (but not their types).
	// Any inserted object can have arbitrary alignment. That a new object fits, or that max object count is not exceeded, 
	// is checked at runtime. Must be manually destructed.
	template <size_t maxElemCount, size_t size, size_t alignment>
	class Pool {
	public:
		template <typename NextElem, typename... Args>
		auto build(Args&&... args) -> NextElem*
		{
			assert(elemIndex < maxElemCount);

			auto space = size - (currentAddr - pool);
			auto currentAddrVoid = static_cast<void*>(currentAddr);
			currentAddr = static_cast<char*>(std::align(alignof(NextElem), sizeof(NextElem), currentAddrVoid, space));
			assert(currentAddr != nullptr);

			auto* nextElem = new (currentAddr) NextElem{ std::forward<Args>(args)... };
			elemPtrs[elemIndex] = currentAddr;
			++elemIndex;

			currentAddr += sizeof(NextElem);
			currentAddrVoid = static_cast<void*>(currentAddr);
			currentAddr = static_cast<char*>(std::align(alignment, 1, currentAddrVoid, space));
			assert(currentAddr != nullptr);

			return nextElem;
		}

		template <typename... ElemTypes>
		void destruct() {
			// can't provide a normal destructor, since destruction requires knowing the types of the elems
			assert(elemIndex == sizeof...(ElemTypes));
			auto n = 0;
			int dummy = (
				std::destroy_at(static_cast<ElemTypes*>(static_cast<void*>(elemPtrs[n++]))),
				..., 0
			);
		}

		auto getElemPtrs() { return std::span{ elemPtrs.begin(), size_t(elemIndex) }; }

	private:
		alignas(alignment) char pool[size];
		char* currentAddr = pool;
		std::array<char*, maxElemCount> elemPtrs;
		int elemIndex = 0;
	};


	// ~ Functional ~
	template <typename Ret, typename... Args>
	constexpr auto returnsAnything(Ret(*)(Args...)) {
		return !std::same_as<Ret, void>;
	}
	template <typename Ret, typename Class, typename... Args>
	constexpr auto returnsAnything(Ret(Class::*)(Args...)) {
		return !std::same_as<Ret, void>;
	}
	template <typename Ret, typename Class, typename... Args>
	constexpr auto returnsAnything(Ret(Class::*)(Args...) const) {
		return !std::same_as<Ret, void>;
	}
	template <typename Func, typename... Args>
	auto derefInvoke(const Func& f, Args&&... args) {
		return std::invoke(f, *std::forward<Args>(args)...);
	};


	// ~ Type reordering ~
	template <typename Invocable, typename... Args, int... Indices>
	constexpr auto invocableOutOfOrder(std::integer_sequence<int, Indices...>) {
		return std::invocable<Invocable, std::tuple_element_t<Indices, std::tuple<Args&...>>...>;
	}
	template <typename Tuple, int... Indices>
	constexpr auto reorderTupleHelper(std::integer_sequence<int, Indices...>) -> std::tuple< std::tuple_element_t<Indices, Tuple>... >* {
		return nullptr;
	}
	template <typename Tuple, typename IndicesPack>
	using ReorderedTuple = std::remove_pointer_t<std::decay_t<decltype(reorderTupleHelper<Tuple>(IndicesPack{}))>>;

	template <template<typename...> typename Target, typename... Ts>
	constexpr auto unwrapTupleHelper(std::tuple<Ts...>*) -> Target<Ts...>* {
		return nullptr;
	}
	template <template<typename...> typename Target, typename Tuple>
	using UnwrapTuple = std::remove_pointer_t<std::decay_t<decltype(unwrapTupleHelper<Target>((Tuple*)nullptr))>>;
		// ^ names the type Target<TupleElem0, TupleElem1, ...>

	template <typename Target, int... Indices>
	auto unwrapIntegerSequence(std::integer_sequence<int, Indices...>) {
		return Target{ Indices... };
	}

	template <template<typename> typename Pred, typename... Ts, int... TrueIndices, int... FalseIndices, int... InverseIndices>
	constexpr auto typePartitionStep(std::integer_sequence<int, TrueIndices...>, std::integer_sequence<int, FalseIndices...>, std::integer_sequence<int, InverseIndices...>)
	{
		constexpr auto currentIdx = sizeof...(TrueIndices) + sizeof...(FalseIndices);

		// If all types were processed, return the result
		if constexpr (sizeof...(Ts) == currentIdx) {
			constexpr auto translateReverseIndex = [](int index) constexpr -> int {
				if (index < 0) {
					return sizeof...(TrueIndices) - 1 - index;
				}
				else {
					return index;
				}
			};
			return std::pair<
				std::integer_sequence<int, TrueIndices..., FalseIndices...>,
				std::integer_sequence<int, translateReverseIndex(InverseIndices)...>
			>{};
		}
		// Else defer to the next step
		else {
			using CurrentT = std::tuple_element_t<currentIdx, std::tuple<Ts...>>;

			if constexpr (std::derived_from<Pred<CurrentT>, std::true_type>) {
				return typePartitionStep<Pred, Ts...>(
					std::integer_sequence<int, TrueIndices..., currentIdx>{},
					std::integer_sequence<int, FalseIndices...>{},
					std::integer_sequence<int, InverseIndices..., sizeof...(TrueIndices)>{}
				);
			}
			else {
				return typePartitionStep<Pred, Ts...>(
					std::integer_sequence<int, TrueIndices...>{},
					std::integer_sequence<int, FalseIndices..., currentIdx>{},
					std::integer_sequence<int, InverseIndices..., -(int)sizeof...(FalseIndices) - 1>{}
				);
			}
		}
	}
	template <template<typename> typename Pred, typename... Ts>
	using TypePartition = std::decay_t<decltype(typePartitionStep<Pred, Ts...>(std::integer_sequence<int>{}, std::integer_sequence<int>{}, std::integer_sequence<int>{})) > ;
		// ^ a pair containing
		//	- a sequence of indices giving the types an order which is partitioned by Pred
		//	- a sequence of indices for the inverse operation, turning the new order back to the original


	// ~ Misc ~
	static constexpr void ignore(const void*) {}

	inline auto getMainThread(lua_State* ls) -> lua_State* {
		lua_rawgeti(ls, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
		auto res = lua_tothread(ls, -1);
		lua_pop(ls, 1);
		return res;
	}

	inline auto popIfOnTop(lua_State* ls, int idx) {
		if (idx == lua_gettop(ls)) {
			lua_pop(ls, 1);
		}
	}

	template <int... Idxs>
	auto reorderLuaStack(lua_State* ls, std::integer_sequence<int, Idxs...>) {
		assert(lua_gettop(ls) == sizeof...(Idxs));
		lua_checkstack(ls, sizeof...(Idxs));
		int dummy[] = { (lua_pushvalue(ls, Idxs + 1), 0)... };
		for (auto i = sizeof...(Idxs); i > 0; --i) {
			lua_remove(ls, i);
		}
	}

	template <typename T, T... vals>
	constexpr auto reverseIotaSequence(std::integer_sequence<T, vals...>) {
		constexpr auto arr = std::array<T, sizeof...(vals)>{ vals... };
		return std::integer_sequence<T, arr[sizeof...(vals) - arr[vals] - 1]...>{};
	}

	template <typename T, std::output_iterator<T> It>
	auto outputItToSinkFunc(It it) {
		return [it](auto&& val) mutable requires std::indirectly_writable<It, decltype(val)> {
			*it = std::forward<decltype(val)>(val);
			++it;
		};
	}

	inline void clearTable(lua_State* ls, int absIdx) {		// [-0, +0, m]
		lua_checkstack(ls, 3);
		lua_pushnil(ls);
		while (lua_next(ls, absIdx)) {
			// -2 = key, -1 = val
			lua_pop(ls, 1);
			lua_pushvalue(ls, -1);
			// -2 = key, -1 = key
			lua_pushnil(ls);
			lua_settable(ls, absIdx);
			// -1 = key
		};
	}

	template <typename... Ts>
	static auto retTypePackHelper() {
		assert(false);
		if constexpr (sizeof...(Ts) == 0); // return nothing
		else if constexpr (sizeof...(Ts) == 1) return static_cast<std::tuple_element_t<0, std::tuple<Ts...>>*>(nullptr);
		else if constexpr (sizeof...(Ts) == 2) return static_cast<std::pair<Ts...>*>(nullptr);
		else return static_cast<std::tuple<Ts...>*>(nullptr);
	}
	template <typename... Ts>
	using PackIfNeccessary = std::remove_pointer_t<std::invoke_result_t<decltype(retTypePackHelper<Ts...>)>>;

	// ~ Errors ~
	inline auto failedToEmplaceError(std::string_view typeName) {
		using namespace std::string_literals;
		auto err = "An argument which is taken by mutable reference must have 'emplace' defined in its LuaStrap type traits.\nArgument type: "s;
		err += typeName;
		err += "\nAlternatively, pass the argument as baked data, not as lua data.\n";
		return err;
	}
	inline auto wrongArgumentCountError(int minArgCount, int maxArgCount, int argCount) {
		using namespace std::string_literals;
		if (minArgCount == maxArgCount) {
			auto err = "Wrong number of arguments. Expected "s;
			err += std::to_string(minArgCount);
			err += ", got ";
			err += std::to_string(argCount);
			err += ".";
			return err;
		}
		else {
			auto err = "Wrong number of arguments. Expected between "s;
			err += std::to_string(minArgCount);
			err += " and ";
			err += std::to_string(maxArgCount);
			err += ", got ";
			err += std::to_string(argCount);
			err += ".";
			return err;
		}
	}
	template <typename... Args>
	auto noMatchingOverloadError(lua_State* ls, int* argOrder, std::string_view msg) {
		using namespace std::string_literals;
		auto argOrderPtr = argOrder;
		auto lines = std::array<std::pair<int, const char*>, sizeof...(Args)> {
			std::pair{ *(argOrderPtr++) + 1, typeid(Args).name() }...
		};

		std::sort(lines.begin(), lines.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

		auto errStr = std::string{ msg } + "\n";
		for (const auto& errorArg : lines) {
			errStr += "#";
			errStr += std::to_string(errorArg.first);
			errStr += " ";
			errStr += errorArg.second;
			errStr += "\n";
		}
		return errStr;
	}
	template <typename Builder>
	auto failedToReadError(lua_State* ls, int argLuaIdx) {
		auto errStr = std::string{ "Wrong format of argument #" } + std::to_string(argLuaIdx) +
			"\nBuilder: " + typeid(Builder).name() +
			"\nArgument type: " + lua_typename(ls, lua_type(ls, argLuaIdx)) +
			"\n";
		return errStr;
	}
	inline auto stackFuncWrongReturnCount(int expected, int got) {
		using namespace std::string_literals;
		auto errStr = "Function referred to by 'StackFunc' returned the wrong number of arguments. Expected "s;
		errStr += std::to_string(expected);
		errStr += ", got ";
		errStr += std::to_string(got);
		errStr += ".\n";
		return errStr;
	}
	template <typename... RetTypes>
	auto stackFuncWrongReturnTypes() {
		using namespace std::string_literals;
		return "Function didn't return what it was supposed to. (Expected return types: \n"s
			+ ((std::string{ typeid(RetTypes).name() } + "\n") + ...) + ")\n";
	}
}
