#include "LuaStrap.h"
#include <algorithm>
#include <numeric>

namespace LuaStrap {

std::deque<StackArrayElem> ArrayIterator::nonOwners;

template <typename Dest, typename... Args>
auto pass(Args... args) {
	return Dest{}(args...);
}

static void refHoldingMetatable(lua_State* ls, std::map<lua_State*, int>& refsPerLs) {
	// Metatable for userdata holding a reference to lua registry.
	// Is created once per lua_State, then only fetched.

	auto mainThread = getMainThread(ls);
	lua_checkstack(ls, 2);

	if (auto ref = refsPerLs.find(mainThread); ref == refsPerLs.end()) {
		lua_createtable(ls, 0, 0); {
			lua_pushcfunction(ls, [](lua_State* ls) {
				// (userdatum)
				auto* udata = lua_touserdata(ls, 1);
				assert(udata != nullptr);
				auto ref = *static_cast<int*>(udata);
				luaL_unref(ls, LUA_REGISTRYINDEX, ref);
				return 0;
			});
			lua_setfield(ls, -2, "__gc");
		}
		lua_pushvalue(ls, -1);
		refsPerLs.emplace(mainThread, luaL_ref(ls, LUA_REGISTRYINDEX));
	}
	else {
		lua_rawgeti(ls, LUA_REGISTRYINDEX, ref->second);
	}
}

void PendingData::metatable(lua_State* ls) {
	static auto refsPerLs = std::map<lua_State*, int>{};
	refHoldingMetatable(ls, refsPerLs);
}
void IndirectData::metatable(lua_State* ls) {
	static auto refsPerLs = std::map<lua_State*, int>{};
	refHoldingMetatable(ls, refsPerLs);
}

auto dataDispatch(lua_State* ls, int idx) -> AnyData {
	idx = lua_absindex(ls, idx);
	lua_checkstack(ls, 2);

	if (!lua_isuserdata(ls, idx)) {
		return LuaData{ ls, idx };
	}

	auto hasMt = lua_getmetatable(ls, idx);
	if (!hasMt) {
		lua_pop(ls, 1);
		return FailData{};
	}

	IndirectData::metatable(ls);
	if (lua_rawequal(ls, -2, -1)) {
		lua_pop(ls, 2);	// pop metatables
		return IndirectData{ ls, idx };
	}
	lua_pop(ls, 1);

	PendingData::metatable(ls);
	if (lua_rawequal(ls, -2, -1)) {
		lua_pop(ls, 2);	// pop metatables
		return PendingData{ ls, idx };
	}
	lua_pop(ls, 2); // pop metatables

	return BakedData{ ls, idx };
}

void LuaData::toLuaData() const {
	luaL_error(ls, "Can't convert LuaData to LuaData.");
}
void PendingData::toLuaData() const {
	// -1 = pendingData
	auto refToLuaData = static_cast<int*>(lua_touserdata(ls, 1));
	lua_rawgeti(ls, LUA_REGISTRYINDEX, *refToLuaData);
}
void BakedData::toLuaData() const {
	// -1 = userdata
	lua_checkstack(ls, 3);
	auto hasMt = lua_getmetatable(ls, -1);
	assert(hasMt);

	lua_getfield(ls, -1, "toLuaData");
	assert(lua_iscfunction(ls, -1));

	lua_pushvalue(ls, -3);
	lua_call(ls, 1, 1);

	lua_remove(ls, -2);
}
void IndirectData::toLuaData() const {
	// -1 = indirectData
	lua_checkstack(ls, 2);
	auto ref = *static_cast<int*>(lua_touserdata(ls, -1));
	lua_rawgeti(ls, LUA_REGISTRYINDEX, ref);

	BakedData{ ls, lua_gettop(ls) }.toLuaData();
	lua_remove(ls, -2);
}
void FailData::toLuaData() const {
	assert(false);
}

void LuaData::toBakedData() const {
	lua_checkstack(ls, 3);
	auto hasProperMt = false;
	auto hasMt = lua_getmetatable(ls, -1);
	if (hasMt) {
		lua_getfield(ls, -1, "toBakedData");
		if (lua_iscfunction(ls, -1)) {
			hasProperMt = true;
		}
		else {
			lua_pop(ls, 2);
		}
	}

	if (hasProperMt) {
		// stack: -3 = luarepres, -2 = metatable, -1 = toBakedData
		lua_pushvalue(ls, -3);
		lua_call(ls, 1, 1);

		lua_remove(ls, -2);
	}
	else {
		// stack: -1 = luarepres
		lua_pushvalue(ls, -1);
		auto luaDataStackObj = luaL_ref(ls, LUA_REGISTRYINDEX);
		new (lua_newuserdata(ls, sizeof(int))) int{ luaDataStackObj }; {
			PendingData::metatable(ls);
			lua_setmetatable(ls, -2);
		}
	}
}
void PendingData::toBakedData() const {
	luaL_error(ls, "The data is already marked for baking.");
}
void BakedData::toBakedData() const {
	luaL_error(ls, "The data is already baked.");
}
void IndirectData::toBakedData() const {
	luaL_error(ls, "The data is already baked.");
}
void FailData::toBakedData() const {
	assert(false);
}

void publishLuaStrapUtils(lua_State* ls) {
	lua_pushcfunction(ls, [](lua_State* ls) {
		// (userdata)
		if (lua_gettop(ls) == 0) {
			return luaL_error(ls, "No arguments provided.");
		}
		lua_settop(ls, 1);
		dataDispatch(ls, 1).toLuaData();
		return 1;
	});
	lua_setfield(ls, -2, "unbaked");

	lua_pushcfunction(ls, [](lua_State* ls) {
		// (luarepres)
		if (lua_gettop(ls) == 0) {
			return luaL_error(ls, "No arguments provided.");
		}
		lua_settop(ls, 1);
		dataDispatch(ls, 1).toBakedData();
		return 1;
	});
	lua_setfield(ls, -2, "markedForBaking");
}

void publishStl(lua_State* ls) {

	// Note: Since the address of an std function shall not be taken, referring to them requires a workaround.
	// Hence the Wrapper, the 'pass' function, and the gratuitous lambdas.

	#define Wrapper(funcName) []<typename... Args>(Args&&... args) { return funcName(std::forward<Args>(args)...); }

	// Non-modifying sequence operations
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::all_of(first, last, pred);
	}); lua_setfield(ls, -2, "all_of");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::any_of(first, last, pred);
	}); lua_setfield(ls, -2, "any_of");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::none_of(first, last, pred);
	}); lua_setfield(ls, -2, "none_of");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackObj value) {
		return std::count(first, last, value);
	}); lua_setfield(ls, -2, "count");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::count_if(first, last, pred);
	}); lua_setfield(ls, -2, "count_if");
	pushFunc(ls, +[](ArrayIterator first1, ArrayIterator last1, ArrayIterator first2, ArrayIterator last2) {
		return std::mismatch(first1, last1, first2, last2);
	}); lua_setfield(ls, -2, "mismatch");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackObj value) {
		return std::find(first, last, value);
	}); lua_setfield(ls, -2, "find");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::find_if(first, last, pred);
	}); lua_setfield(ls, -2, "find_if");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::find_if_not(first, last, pred);
	}); lua_setfield(ls, -2, "find_if_not");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator s_first, ArrayIterator s_last) {
		return std::find_end(first, last, s_first, s_last);
	}); lua_setfield(ls, -2, "find_end");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator s_first, ArrayIterator s_last) {
		return std::find_first_of(first, last, s_first, s_last);
	}); lua_setfield(ls, -2, "find_first_of");
	{
		using Trans = decltype(Wrapper(std::adjacent_find));
		pushOverloadedFunc(ls,
			&pass<Trans, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Trans, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "adjacent_find");
	}
	{
		using Trans = decltype(Wrapper(std::search));
		pushOverloadedFunc(ls,
			&pass<Trans, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Trans, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "search");
	}
	{
		using Trans = decltype(Wrapper(std::search_n));
		pushOverloadedFunc(ls,
			&pass<Trans, ArrayIterator, ArrayIterator, int, StackObj, StackFunc<bool>>,
			&pass<Trans, ArrayIterator, ArrayIterator, int, StackObj>
		); lua_setfield(ls, -2, "search_n");
	}

	// Modifying sequence operations
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator d_first) {
		return std::copy(first, last, d_first);
	}); lua_setfield(ls, -2, "copy");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator d_first, StackFunc<bool> pred) {
		return std::copy_if(first, last, d_first, pred);
	}); lua_setfield(ls, -2, "copy_if");
	pushFunc(ls, +[](ArrayIterator first, int count, ArrayIterator result) {
		return std::copy_n(first, count, result);
	}); lua_setfield(ls, -2, "copy_n");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator d_last) {
		return std::copy_backward(first, last, d_last);
	}); lua_setfield(ls, -2, "copy_backward");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackObj value) {
		return std::fill(first, last, value);
	}); lua_setfield(ls, -2, "fill");
	pushFunc(ls, +[](ArrayIterator first, int count, StackObj value) {
		return std::fill_n(first, count, value);
	}); lua_setfield(ls, -2, "fill_n");

	{
		using Trans = decltype(Wrapper(std::transform));
		pushOverloadedFunc(ls,
			&pass<Trans, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<StackObj>>,
			&pass<Trans, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<StackObj>>
		); lua_setfield(ls, -2, "transform");
	}

	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<StackObj> gen) {
		return std::generate(first, last, gen);
	}); lua_setfield(ls, -2, "generate");
	pushFunc(ls, +[](ArrayIterator first, int count, StackFunc<StackObj> gen) {
		return std::generate_n(first, count, gen);
	}); lua_setfield(ls, -2, "generate_n");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackObj val) {
		return std::remove(first, last, val);
	}); lua_setfield(ls, -2, "remove");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::remove_if(first, last, pred);
	}); lua_setfield(ls, -2, "remove_if");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator d_first, StackObj val) {
		return std::remove_copy(first, last, d_first, val);
	}); lua_setfield(ls, -2, "remove_copy");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator d_first, StackFunc<bool> pred) {
		return std::remove_copy_if(first, last, d_first, pred);
	}); lua_setfield(ls, -2, "remove_copy_if");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackObj old_value, StackObj new_value) {
		return std::replace(first, last, old_value, new_value);
	}); lua_setfield(ls, -2, "replace");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred, StackObj new_value) {
		return std::replace_if(first, last, pred, new_value);
	}); lua_setfield(ls, -2, "replace_if");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator d_first, StackObj old_value, StackObj new_value) {
		return std::replace_copy(first, last, d_first, old_value, new_value);
	}); lua_setfield(ls, -2, "replace_copy");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator d_first, StackFunc<bool> pred, StackObj new_value) {
		return std::replace_copy_if(first, last, d_first, pred, new_value);
	}); lua_setfield(ls, -2, "replace_copy_if");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator first2) {
		return std::swap_ranges(first, last, first2);
	}); lua_setfield(ls, -2, "swap_ranges");
	pushFunc(ls, +[](ArrayIterator a, ArrayIterator b) {
		return std::iter_swap(a, b);
	}); lua_setfield(ls, -2, "iter_swap");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last) {
		return std::reverse(first, last);
	}); lua_setfield(ls, -2, "reverse");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator d_first) {
		return std::reverse_copy(first, last, d_first);
	}); lua_setfield(ls, -2, "reverse_copy");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator middle, ArrayIterator last) {
		return std::rotate(first, middle, last);
	}); lua_setfield(ls, -2, "rotate");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator n_first, ArrayIterator last, ArrayIterator d_first) {
		return std::rotate_copy(first, n_first, last, d_first);
	}); lua_setfield(ls, -2, "rotate_copy");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, int n) {
		return std::shift_left(first, last, n);
	}); lua_setfield(ls, -2, "shift_left");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, int n) {
		return std::shift_right(first, last, n);
	}); lua_setfield(ls, -2, "shift_right");
	{
		using Func = decltype(Wrapper(std::unique));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "unique");
	}
	{
		using Func = decltype(Wrapper(std::unique_copy));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "unique_copy");
	}

	// Partitioning operations
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::is_partitioned(first, last, pred);
	}); lua_setfield(ls, -2, "is_partitioned");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::partition(first, last, pred);
	}); lua_setfield(ls, -2, "partition");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, ArrayIterator first_true, ArrayIterator first_false, StackFunc<bool> pred) {
		return std::partition_copy(first, last, first_true, first_false, pred);
	}); lua_setfield(ls, -2, "partition_copy");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::stable_partition(first, last, pred);
	}); lua_setfield(ls, -2, "stable_partition");
	pushFunc(ls, +[](ArrayIterator first, ArrayIterator last, StackFunc<bool> pred) {
		return std::partition_point(first, last, pred);
	}); lua_setfield(ls, -2, "partition_point");

	// Sorting operations
	{
		using Func = decltype(Wrapper(std::is_sorted));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "is_sorted");
	}
	{
		using Func = decltype(Wrapper(std::is_sorted_until));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "is_sorted_until");
	}
	{
		using Func = decltype(Wrapper(std::sort));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "sort");
	}
	{
		using Func = decltype(Wrapper(std::partial_sort));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "partial_sort");
	}
	{
		using Func = decltype(Wrapper(std::partial_sort_copy));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "partial_sort_copy");
	}
	{
		using Func = decltype(Wrapper(std::stable_sort));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "stable_sort");
	}
	{
		using Func = decltype(Wrapper(std::nth_element));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "nth_element");
	}

	// Binary search operations (on sorted ranges)
	{
		using Func = decltype(Wrapper(std::lower_bound));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "lower_bound");
	}
	{
		using Func = decltype(Wrapper(std::upper_bound));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "upper_bound");
	}
	{
		using Func = decltype(Wrapper(std::binary_search));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "binary_search");
	}
	{
		using Func = decltype(Wrapper(std::equal_range));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "equal_range");
	}

	// Other operations on sorted ranges
	{
		using Func = decltype(Wrapper(std::merge));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "merge");
	}
	{
		using Func = decltype(Wrapper(std::inplace_merge));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "inplace_merge");
	}

	// Set operations (on sorted ranges)
	{
		using Func = decltype(Wrapper(std::includes));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "includes");
	}
	{
		using Func = decltype(Wrapper(std::set_difference));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "set_difference");
	}
	{
		using Func = decltype(Wrapper(std::set_intersection));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "set_intersection");
	}
	{
		using Func = decltype(Wrapper(std::set_symmetric_difference));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "set_symmetric_difference");
	}
	{
		using Func = decltype(Wrapper(std::set_union));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "set_union");
	}

	// Heap operations
	{
		using Func = decltype(Wrapper(std::is_heap));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "is_heap");
	}
	{
		using Func = decltype(Wrapper(std::is_heap_until));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "is_heap_until");
	}
	{
		using Func = decltype(Wrapper(std::make_heap));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "make_heap");
	}
	{
		using Func = decltype(Wrapper(std::push_heap));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "push_heap");
	}
	{
		using Func = decltype(Wrapper(std::pop_heap));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "pop_heap");
	}
	{
		using Func = decltype(Wrapper(std::sort_heap));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "sort_heap");
	}

	// Minimum/maximum operations
	{
		using Func = decltype(Wrapper(std::max_element));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "max_element");
	}
	{
		using Func = decltype(Wrapper(std::min_element));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "min_element");
	}
	{
		using Func = decltype(Wrapper(std::minmax));
		pushOverloadedFunc(ls,
			&pass<Func, StackObj, StackObj, StackFunc<bool>>,
			&pass<Func, StackObj, StackObj>
		); lua_setfield(ls, -2, "minmax");
	}
	{
		using Func = decltype(Wrapper(std::minmax_element));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "minmax_element");
	}
	{
		using Func = decltype(Wrapper(std::clamp));
		pushOverloadedFunc(ls,
			&pass<Func, StackObj, StackObj, StackObj, StackFunc<bool>>,
			&pass<Func, StackObj, StackObj, StackObj>
		); lua_setfield(ls, -2, "clamp");
	}

	// Comparison operations
	{
		using Func = decltype(Wrapper(std::equal));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "equal");
	}
	{
		using Func = decltype(Wrapper(std::lexicographical_compare));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "lexicographical_compare");
	}

	// Permutation operations
	{
		using Func = decltype(Wrapper(std::is_permutation));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, ArrayIterator>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "is_permutation");
	}
	{
		using Func = decltype(Wrapper(std::next_permutation));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "next_permutation");
	}
	{
		using Func = decltype(Wrapper(std::prev_permutation));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackFunc<bool>>,
			&pass<Func, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "prev_permutation");
	}


	// Numeric operations
	{
		using Func = decltype(Wrapper(std::iota));
		pushFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "iota");
	}
	{
		using Func = decltype(Wrapper(std::accumulate));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "accumulate");
	}
	{
		using Func = decltype(Wrapper(std::inner_product));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackObj, StackFunc<StackObj>, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "inner_product");
	}
	{
		using Func = decltype(Wrapper(std::adjacent_difference));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "adjacent_difference");
	}
	{
		using Func = decltype(Wrapper(std::partial_sum));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "partial_sum");
	}
	{
		using Func = decltype(Wrapper(std::reduce));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj>
			// &pass<Func, ArrayIterator, ArrayIterator>
			// ^ not provided, because there is no reasonable default value for a lua variable
		); lua_setfield(ls, -2, "reduce");
	}
	{
		using Func = decltype(Wrapper(std::exclusive_scan));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackObj, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "exclusive_scan");
	}
	{
		using Func = decltype(Wrapper(std::inclusive_scan));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<StackObj>, StackObj>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator>
		); lua_setfield(ls, -2, "inclusive_scan");
	}
	{
		using Func = decltype(Wrapper(std::transform_reduce));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackObj, StackFunc<StackObj>, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, StackObj, StackFunc<StackObj>, StackFunc<StackObj>>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackObj>
		); lua_setfield(ls, -2, "transform_reduce");
	}
	{
		using Func = decltype(Wrapper(std::transform_exclusive_scan));
		pushFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackObj, StackFunc<StackObj>, StackFunc<StackObj>>
		); lua_setfield(ls, -2, "transform_exclusive_scan");
	}
	{
		using Func = decltype(Wrapper(std::transform_inclusive_scan));
		pushOverloadedFunc(ls,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<StackObj>, StackFunc<StackObj>, StackObj>,
			&pass<Func, ArrayIterator, ArrayIterator, ArrayIterator, StackFunc<StackObj>, StackFunc<StackObj>>
		); lua_setfield(ls, -2, "transform_inclusive_scan");
	}

	#undef Wrapper
}

}	// end namespace LuaStrap
