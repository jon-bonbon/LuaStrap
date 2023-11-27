#pragma once
#include "CppLuaInterface.h"
#include <functional>
#include <deque>
#include <iostream>
#include <utility>

namespace LuaStrap {

	// StackValue types provide c++ semantics to lua values lying on stack.
	// Intended to allow c++ algorithms to deal with lua entities - not meant to be used directly in user code.
	// They can be compared and arithmetically manipulated, some of them treated as bools or invoked.
	// Whether they have value or reference semantics depends on the particular type.

	template <typename T>
	concept StackValue = requires(const T t) {
		{ t.ls } -> std::convertible_to<lua_State*>;
		{ t.locateOnStack() } -> std::same_as<std::pair<int, bool>>;	// [-0, +0 or +1, m]
			// ^ ret.first = the absolute stack index of the value
			//   ret.second = whether the value was just pushed (in which case it's on stack top)
	};
	void pushStackValue(const StackValue auto& val) {		// [-0, +1, m]
		auto [valIdx, didPush] = val.locateOnStack();
		if (!didPush) {
			lua_pushvalue(val.ls, valIdx);
		}
	}

	// Refers to any value
	class StackObj {
	public:
		lua_State* const ls;
		int idx;

		StackObj(lua_State* ls, int idx) : ls{ ls }, idx{ lua_absindex(ls, idx) } {}	// [-0, +0]				
		auto locateOnStack() const { return std::pair{ idx, false }; }					// [-0, +0]

		auto operator=(const StackObj& rhs) -> StackObj& {			// [-0, +0]
			assert(ls == rhs.ls);
			pushStackValue(rhs);
			lua_replace(ls, idx);
			return *this;
		}
		auto operator=(const StackValue auto& rhs) -> StackObj& {	// [-0, +0]
			assert(ls == rhs.ls);
			pushStackValue(rhs);
			lua_replace(ls, idx);
			return *this;
		}
		auto operator++() -> StackObj& {							// [-0, +0]
			assert(lua_isinteger(ls, idx));
			lua_pushinteger(ls, lua_tointeger(ls, idx) + 1);
			lua_replace(ls, idx);
			return *this;
		}
		auto operator--() -> StackObj& {							// [-0, +0]
			assert(lua_isinteger(ls, idx));
			lua_pushinteger(ls, lua_tointeger(ls, idx) - 1);
			lua_replace(ls, idx);
			return *this;
		}
		// post-increment not easily implementable
		operator bool() const {										// [-0, +0]
			return lua_toboolean(ls, idx);
		}
	};
	static_assert(StackValue<StackObj>);

	// Refers to a function
	template <typename... RetTypes>
	class StackFunc : public StackObj {
	public:
		using StackObj::StackObj;

		auto operator()(const StackValue auto&... args) const {		// [-0, +n, e]
			assert(lua_isfunction(ls, idx));
			auto origTop = lua_gettop(ls);

			// Push the function
			lua_pushvalue(ls, idx);

			// Push the arguments
			if constexpr (sizeof...(args) > 0) {
				int dummy[] = { (pushStackValue(args), 0)... };
				ignore(dummy);
			}

			// Call, evaluate and clean up
			lua_call(ls, sizeof...(args), LUA_MULTRET);
			if (lua_gettop(ls) != origTop + sizeof...(RetTypes)) {
				luaL_error(ls, stackFuncWrongReturnCount(sizeof...(RetTypes), lua_gettop(ls) - origTop).c_str());
			}

			if constexpr (sizeof...(RetTypes) > 0) {
				auto idx = origTop;

				using namespace std::string_literals;
				auto readStep = [&]<typename Ret> {
					++idx;
					auto origTop = lua_gettop(ls);
					auto val = LuaStrap::read<Ret>(ls, idx);
					if (lua_gettop(ls) != origTop) {
						luaL_error(ls, "Types which push on read must not be returned from a function referred to by 'StackFunc'.");
					}
					if (!val) {
						luaL_error(ls, stackFuncWrongReturnTypes<RetTypes...>().c_str());
					}
					return std::move(*val);
				};
				auto res = PackIfNeccessary<RetTypes...>{ readStep.template operator()<RetTypes>()... };
				if constexpr (!std::same_as<PackIfNeccessary<RetTypes...>, StackObj>) {
					lua_settop(ls, origTop);
				}
				return res;
			}
		}

	};
	static_assert(StackValue<StackFunc<>>);

	// If directly constructed, refers to an element of an on-stack array.
	// If constructed by copy, refers to its own value.
	class StackArrayElem {
	public:
		constexpr static lua_Integer ownValue = std::numeric_limits<lua_Integer>::min();
		lua_State* const ls;
		const int idx;
		const lua_Integer arrKey;	// key of the referenced elem

		StackArrayElem(lua_State* ls, int idx, lua_Integer arrKey) :		// [-0, +0]
			ls{ ls }, idx{ lua_absindex(ls, idx) }, arrKey{ arrKey }
		{}
		StackArrayElem(const StackArrayElem& rhs) :							// [-0, +1]
			ls{ rhs.ls }, idx{ lua_gettop(rhs.ls) + 1 }, arrKey{ ownValue }
		{
			pushStackValue(rhs);
		}
		~StackArrayElem() {													// [-0 or -1, +0]
			lua_pop(ls, arrKey == ownValue && lua_gettop(ls) == idx);
		}
		auto locateOnStack() const {
			if (arrKey == ownValue) {
				return std::pair{ idx, false };
			}
			else {
				lua_geti(ls, idx, arrKey);
				return std::pair{ static_cast<int>(lua_gettop(ls)), true };
			}
		}
		auto operator=(const StackArrayElem& rhs) -> StackArrayElem& {			// [-0, +0]
			assert(ls == rhs.ls);

			pushStackValue(rhs);

			if (arrKey == ownValue) {
				lua_replace(ls, idx);
			}
			else {
				lua_rawseti(ls, idx, arrKey);
			}

			return *this;
		}
		auto operator=(const StackValue auto& rhs) -> StackArrayElem& {			// [-0, +0]
			assert(ls == rhs.ls);

			pushStackValue(rhs);

			if (arrKey == ownValue) {
				lua_replace(ls, idx);
			}
			else {
				lua_rawseti(ls, idx, arrKey);
			}

			return *this;
		}
	};
	static_assert(StackValue<StackArrayElem>);

	// Comparisons
	auto cmpHelper(const StackValue auto& lhs, const StackValue auto& rhs, auto cmp) {
		auto [lIdx, lDidPush] = lhs.locateOnStack();
		auto [rIdx, rDidPush] = rhs.locateOnStack();
		auto res = cmp(lhs.ls, lIdx, rIdx);
		lua_pop(lhs.ls, static_cast<int>(lDidPush) + static_cast<int>(rDidPush));
		return res;
	}
	auto operator==(const StackValue auto& lhs, const StackValue auto& rhs) {
		return cmpHelper(lhs, rhs, [](lua_State* ls, int lIdx, int rIdx) {
			return lua_rawequal(ls, lIdx, rIdx);
		});
	} auto operator!=(const StackValue auto& lhs, const StackValue auto& rhs) {
		return !(lhs == rhs);
	} auto operator<(const StackValue auto& lhs, const StackValue auto& rhs) {
		return cmpHelper(lhs, rhs, [](lua_State* ls, int lIdx, int rIdx) {
			return lua_compare(ls, lIdx, rIdx, LUA_OPLT);
		});
	} auto operator<=(const StackValue auto& lhs, const StackValue auto& rhs) {
		return cmpHelper(lhs, rhs, [](lua_State* ls, int lIdx, int rIdx) {
			return lua_compare(ls, lIdx, rIdx, LUA_OPLE);
		});
	} auto operator>(const StackValue auto& lhs, const StackValue auto& rhs) {
		return cmpHelper(lhs, rhs, [](lua_State* ls, int lIdx, int rIdx) {
			return lua_compare(ls, rIdx, lIdx, LUA_OPLT);
		});
	} auto operator>=(const StackValue auto& lhs, const StackValue auto& rhs) {
		return cmpHelper(lhs, rhs, [](lua_State* ls, int lIdx, int rIdx) {
			return lua_compare(ls, rIdx, lIdx, LUA_OPLE);
		});
	}

	// Arithmetic ops
	auto numberOp(const StackValue auto& lhs, const StackValue auto& rhs, auto op) -> StackObj {
		assert(lhs.ls == rhs.ls);
		auto [lIdx, lDidPush] = lhs.locateOnStack();
		auto [rIdx, rDidPush] = rhs.locateOnStack();

		auto lType = lua_type(lhs.ls, lIdx);
		auto rType = lua_type(rhs.ls, rIdx);
		assert(lType == LUA_TNUMBER && rType == LUA_TNUMBER);
		auto lVal = lua_tonumber(lhs.ls, lIdx);
		auto rVal = lua_tonumber(rhs.ls, rIdx);
		lua_pop(lhs.ls, static_cast<int>(lDidPush) + static_cast<int>(rDidPush));
		lua_pushnumber(lhs.ls, op(lVal, rVal));
		return StackObj{ lhs.ls, lua_gettop(lhs.ls) };
	}
	auto operator+(const StackValue auto& lhs, const StackValue auto& rhs) {	// [-0, +1]
		return numberOp(lhs, rhs, std::plus<>{});
	}
	auto operator-(const StackValue auto& lhs, const StackValue auto& rhs) {	// [-0, +1]
		return numberOp(lhs, rhs, std::minus<>{});
	}
	auto operator*(const StackValue auto& lhs, const StackValue auto& rhs) {	// [-0, +1]
		return numberOp(lhs, rhs, std::multiplies<>{});
	}
	auto operator/(const StackValue auto& lhs, const StackValue auto& rhs) {	// [-0, +1]
		return numberOp(lhs, rhs, std::divides<>{});
	}

	// Array iteration
	class ArrayIterator {
	public:
		using difference_type = lua_Integer;
		using value_type = StackArrayElem;

		static std::deque<StackArrayElem> nonOwners;
		static void clearGarbage() { nonOwners.clear(); }

		lua_State* ls;
		int tableIdx;		// where the table being iterated through is
		lua_Integer key;	// the current key

		ArrayIterator() : ls{ nullptr } {}
		ArrayIterator(lua_State* ls, int tableIdx, lua_Integer key) :
			ls{ ls }, tableIdx{ lua_absindex(ls, tableIdx) }, key{ key } {}

		auto operator<=>(const ArrayIterator& rhs) const -> std::weak_ordering {
			assert(ls == rhs.ls);
			assert(ls != nullptr);
			assert(tableIdx == rhs.tableIdx || lua_rawequal(ls, tableIdx, rhs.tableIdx));
			return key <=> rhs.key;
		}
		auto operator==(const ArrayIterator& rhs) const -> bool {
			assert(ls == rhs.ls);
			assert(ls != nullptr);
			assert(tableIdx == rhs.tableIdx || lua_rawequal(ls, tableIdx, rhs.tableIdx));
			return key == rhs.key;
		}

		auto operator*() const -> StackArrayElem& {
			assert(ls != nullptr);
			nonOwners.emplace_back(ls, tableIdx, key);
			return nonOwners.back();
		}
		auto operator[](difference_type offset) const -> StackArrayElem& {
			assert(ls != nullptr);
			nonOwners.emplace_back(ls, tableIdx, key + offset);
			return nonOwners.back();
		}

		auto operator++() -> ArrayIterator& {
			assert(ls != nullptr);
			++key;
			return *this;
		}
		auto operator++(int) -> ArrayIterator { auto orig = *this; ++(*this); return orig; }
		auto operator--() -> ArrayIterator& {
			assert(ls != nullptr);
			--key;
			return *this;
		}
		auto operator--(int) -> ArrayIterator { auto orig = *this; --(*this); return orig; }
		friend auto operator+=(ArrayIterator& lhs, lua_Integer offset) -> ArrayIterator& {
			lhs.key += offset;
			return lhs;
		}
		friend auto operator+(const ArrayIterator& lhs, difference_type offset) -> ArrayIterator {
			return ArrayIterator{ lhs.ls, lhs.tableIdx, lhs.key + offset };
		}
		friend auto operator+(difference_type offset, const ArrayIterator& rhs) -> ArrayIterator {
			return ArrayIterator{ rhs.ls, rhs.tableIdx, offset + rhs.key };
		}
		friend auto operator-=(ArrayIterator& lhs, difference_type offset) -> ArrayIterator& {
			lhs.key -= offset;
			return lhs;
		}
		friend auto operator-(const ArrayIterator& lhs, difference_type offset) -> ArrayIterator {
			return ArrayIterator{ lhs.ls, lhs.tableIdx, lhs.key - offset };
		}
		friend auto operator-(const ArrayIterator& lhs, const ArrayIterator& rhs) -> difference_type {
			return lhs.key - rhs.key;
		}
	};
	static_assert(std::random_access_iterator<ArrayIterator>);

	template <>
	struct Traits<StackObj> {
		static auto read(lua_State* ls, int idx) -> std::optional<StackObj> {		// [-0, +0]
			return StackObj{ ls, idx };
		}
		static void write(lua_State* ls, const StackObj& v) {						// [-0, +1]
			lua_pushvalue(ls, v.idx);
		}
	};
	template <typename... RetTypes>
	struct Traits<StackFunc<RetTypes...>> {
		static auto read(lua_State* ls, int idx) -> std::optional<StackFunc<RetTypes...>> {		// [-0, +0]
			if (!lua_isfunction(ls, idx)) {
				lua_pop(ls, 1);
				return std::nullopt;
			}
			return StackFunc<RetTypes...>{ ls, idx };
		}
		static void write(lua_State* ls, const StackFunc<RetTypes...>& v) {						// [-0, +1]
			lua_pushvalue(ls, v.idx);
		}
	};
	template <>
	struct Traits<ArrayIterator> {
		// raw { [1] = (table)theArray, [2] = (integer)key }
		static auto read(lua_State* ls, int idx) -> std::optional<ArrayIterator> {		// [-0, +1]
			if (!lua_istable(ls, -1)) {
				return std::nullopt;
			}

			lua_rawgeti(ls, idx, 1);
			lua_rawgeti(ls, idx, 2);
			// stack: -2 = theArray, -1 = key

			if (!lua_istable(ls, -2) || !lua_isinteger(ls, -1)) {
				lua_pop(ls, 2);
				return std::nullopt;
			}
			auto key = lua_tointeger(ls, -1);
			lua_pop(ls, 1);
			// stack: -1 = theArray

			return ArrayIterator{ ls, lua_gettop(ls), key };
		}
		static void emplace(lua_State* ls, const ArrayIterator& v, int idx) {
			lua_pushvalue(ls, v.tableIdx);
			lua_rawseti(ls, idx, 1);
			lua_pushinteger(ls, v.key);
			lua_rawseti(ls, idx, 2);
		}
	};
}
