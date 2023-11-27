
do
	local function defaultCmp(a,b) return a < b end
	local function defaultEq(a,b) return a == b end
	local function shallowEq(a, b)
		local typeA = type(a)
		local typeB = type(b)
		if typeA == "table" and typeB == "table" then
			for key, val in pairs(a) do
				if b[key] ~= val then return false end
			end
			for key, val in pairs(b) do
				if a[key] == nil then return false end
			end
			return true
		else
			return a == b
		end
	end
	
	-- Non-modifying sequence operations
	do
		local tbl = {1, 2, 2, "hello", 1, 2}
		assert(stl.all_of({tbl,1}, {tbl,#tbl+1}, function(v) return v ~= 0 end))
		assert(stl.any_of({tbl,1}, {tbl,#tbl+1}, function(v) return v == "hello" end))
		assert(stl.none_of({tbl,1}, {tbl,#tbl+1}, function(v) return v == 10 end))
		assert(stl.count({tbl,1}, {tbl,#tbl+1}, 2) == 3)
		assert(stl.count_if({tbl,1}, {tbl,#tbl+1}, function(v) return v == 2 end) == 3)

		local tbl2 = {1, 2, 3, 4, 5}
		local mismatch = stl.mismatch({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1})
		assert(mismatch[1][2] == 3 and mismatch[2][2] == 3)
		assert(stl.find({tbl,1}, {tbl,#tbl+1}, "not_present")[2] == #tbl+1)
		assert(stl.find_if({tbl,1}, {tbl,#tbl+1}, function(v) return v == "hello" end)[2] == 4)
		assert(stl.find_if_not({tbl,1}, {tbl,#tbl+1}, function(v) return type(v) == "number" end)[2] == 4)

		local tbl3 = {1, 2}
		assert(stl.find_end({tbl,1}, {tbl,#tbl+1}, {tbl3,1}, {tbl3,#tbl3+1})[2] == 5)

		local tbl4 = {"hello", 2, 10}
		assert(stl.find_first_of({tbl,1}, {tbl,#tbl+1}, {tbl4,1}, {tbl4,#tbl4+1})[2] == 2)
		assert(stl.adjacent_find({tbl,1}, {tbl,#tbl+1})[2] == 2)
		assert(stl.adjacent_find({tbl,1}, {tbl,#tbl+1}, function(a,b) return a == 2 and b == "hello" end)[2] == 3)
		assert(stl.search({tbl,1}, {tbl,#tbl+1}, {tbl3,1}, {tbl3,#tbl3+1})[2] == 1)
		assert(stl.search({tbl,1}, {tbl,#tbl+1}, {tbl3,1}, {tbl3,#tbl3+1}, function(a,b) return a == b end)[2] == 1)
		assert(stl.search_n({tbl,1}, {tbl,#tbl+1}, 2, 2)[2] == 2)
	end

	-- Modifying sequence operations
	do
		local tbl = {10, 20, 30, 40, 50, 60}
		assert(stl.copy({tbl,1}, {tbl,3}, {tbl,4})[2] == 6)
		assert(shallowEq(tbl, {10, 20, 30, 10, 20, 60}))

		local tbl2 = {}
		assert(stl.copy_if({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, function(v) return math.fmod(v,20) < 1 end)[2] == 4)
		assert(shallowEq(tbl2, {20, 20, 60}))

		assert(stl.copy_n({tbl,1}, 2, {tbl2,1})[2] == 3)
		assert(shallowEq(tbl2, {10, 20, 60}))

		assert(stl.copy_backward({tbl2,1}, {tbl2,#tbl2+1}, {tbl2,#tbl2+2})[2] == 2)
		assert(shallowEq(tbl2, {10, 10, 20, 60}))

		stl.fill({tbl2,4}, {tbl2,6}, 50)
		assert(shallowEq(tbl2, {10, 10, 20, 50, 50}))

		assert(stl.fill_n({tbl2,5}, 2, 15)[2] == 7)
		assert(shallowEq(tbl2, {10, 10, 20, 50, 15, 15}))

		assert(stl.transform({tbl2,1}, {tbl2,#tbl2+1}, {tbl2,1}, function(v) return v / 5 end)[2] == #tbl2+1)
		assert(shallowEq(tbl2, {2, 2, 4, 10, 3, 3}))

		assert(stl.transform({tbl,2}, {tbl,6}, {tbl2,1}, {tbl2,1}, function(a,b) return a+b end)[2] == 5)
		assert(shallowEq(tbl2, {22, 32, 14, 30, 3, 3}))

		local tbl3 = {}
		stl.generate({tbl3,1}, {tbl3,11}, coroutine.wrap(function()
			for i=1, 5 do
				coroutine.yield(i)
			end
			coroutine.yield(60)
			coroutine.yield(70)
			coroutine.yield(80)
			coroutine.yield(90)
			coroutine.yield(100)
			coroutine.yield("this won't be reached")
		end))
		assert(shallowEq(tbl3, {1, 2, 3, 4, 5, 60, 70, 80, 90, 100}))

		stl.generate_n({tbl3,1}, 2, function() return 0 end)
		assert(shallowEq(tbl3, {0, 0, 3, 4, 5, 60, 70, 80, 90, 100}))

		-- Lua version of C++'s "erase-remove" idiom
		for idx = stl.remove({tbl3,1}, {tbl3,#tbl3+1}, 0)[2], #tbl3 do
			tbl3[idx] = nil
		end
		assert(shallowEq(tbl3, {3, 4, 5, 60, 70, 80, 90, 100}))

		for idx = stl.remove_if({tbl3,1}, {tbl3,#tbl3+1}, function(v) return v > 10 end)[2], #tbl3 do
			tbl3[idx] = nil
		end
		assert(shallowEq(tbl3, {3, 4, 5}))

		assert(stl.remove_copy({tbl3,1}, {tbl3,#tbl3+1}, {tbl3,#tbl3+1}, 4)[2] == 6)
		assert(shallowEq(tbl3, {3, 4, 5, 3, 5}))

		assert(stl.remove_copy_if({tbl3,1}, {tbl3,#tbl3+1}, {tbl3,#tbl3+1}, function(v) return v > 3 end)[2] == 8)
		assert(shallowEq(tbl3, {3, 4, 5, 3, 5, 3, 3}))

		stl.replace({tbl3,1}, {tbl3,#tbl3+1}, 3, 1)
		assert(shallowEq(tbl3, {1, 4, 5, 1, 5, 1, 1}))

		stl.replace_if({tbl3,1}, {tbl3,#tbl3+1}, function(v) return v < 2 end, 3)
		assert(shallowEq(tbl3, {3, 4, 5, 3, 5, 3, 3}))

		local tbl4 = {}
		assert(stl.replace_copy({tbl3,1}, {tbl3,#tbl3+1}, {tbl4,1}, 3, 1)[2] == 8)
		assert(shallowEq(tbl4, {1, 4, 5, 1, 5, 1, 1}))

		assert(stl.replace_copy_if({tbl3,1}, {tbl3,#tbl3+1}, {tbl4,1}, function(v) return v < 2 end, 3)[2] == 8)
		assert(shallowEq(tbl4, {3, 4, 5, 3, 5, 3, 3}))

		assert(stl.swap_ranges({tbl4,1}, {tbl4,4}, {tbl4,4})[2] == 7)
		assert(shallowEq(tbl4, {3, 5, 3, 3, 4, 5, 3}))

		stl.iter_swap({tbl4,1}, {tbl4,2})
		assert(shallowEq(tbl4, {5, 3, 3, 3, 4, 5, 3}))

		stl.reverse({tbl4,5}, {tbl4,#tbl4+1})
		assert(shallowEq(tbl4, {5, 3, 3, 3, 3, 5, 4}))

		assert(stl.reverse_copy({tbl4,5}, {tbl4,#tbl4+1}, {tbl4,2})[2] == 5)
		assert(shallowEq(tbl4, {5, 4, 5, 3, 3, 5, 4}))

		assert(stl.rotate({tbl4,1}, {tbl4,2}, {tbl4,#tbl4+1})[2] == 7)
		assert(shallowEq(tbl4, {4, 5, 3, 3, 5, 4, 5}))

		assert(stl.rotate_copy({tbl4,1}, {tbl4,2}, {tbl4,4}, {tbl4,4})[2] == 7)
		assert(shallowEq(tbl4, {4, 5, 3, 5, 3, 4, 5}))

		for idx = stl.shift_left({tbl4,1}, {tbl4,#tbl4+1}, 4)[2], #tbl4 do
			tbl4[idx] = nil
		end
		assert(shallowEq(tbl4, {3, 4, 5}))

		for idx = stl.shift_right({tbl4,1}, {tbl4,#tbl4+1}, 1)[2] - 1, 1, -1 do
			table.remove(tbl4, idx)
		end
		assert(shallowEq(tbl4, {3, 4}))

		local tbl5 = {1, 2, 2, 2, 2, 3, 5, 8, 9}
		for idx = stl.unique({tbl5,1}, {tbl5,#tbl5+1})[2], #tbl5 do
			tbl5[idx] = nil
		end
		assert(shallowEq(tbl5, {1, 2, 3, 5, 8, 9}))

		for idx = stl.unique({tbl5,1}, {tbl5,#tbl5+1}, function(a,b) return math.abs(math.fmod(a,2) - math.fmod(b,2)) < 0.01 end)[2], #tbl5 do
			tbl5[idx] = nil
		end
		assert(shallowEq(tbl5, {1, 2, 3, 8, 9}))

		assert(stl.unique_copy({tbl5,1}, {tbl5,3}, {tbl5, 3})[2] == 5)
		assert(shallowEq(tbl5, {1, 2, 1, 2, 9}))

		assert(stl.unique_copy({tbl5,1}, {tbl5,#tbl5+1}, {tbl5, #tbl5+1}, function(a,b) return a < 3 and b < 3 end)[2] == 8)
	end

	-- Partitioning operations
	do
		local tbl = {3, 2, 1, 4, 5, 6}
		assert(stl.is_partitioned({tbl,1}, {tbl,#tbl+1}, function(v) return v < 4 end))
		assert(stl.partition_point({tbl,1}, {tbl,#tbl+1}, function(v) return v < 4 end)[2] == 4)

		assert(stl.stable_partition({tbl,1}, {tbl,#tbl+1}, function(v) return v < 3 end)[2] == 3)
		assert(shallowEq(tbl, {2, 1, 3, 4, 5, 6}))

		assert(stl.partition({tbl,1}, {tbl,#tbl+1}, function(v) return v < 2 end)[2] == 2)
		assert(shallowEq(tbl, {1, 2, 3, 4, 5, 6}))

		local smallValues = {}
		local bigValues = {}
		assert(stl.partition_copy({tbl,1}, {tbl,#tbl+1}, {smallValues,1}, {bigValues,1}, function(v) return v < 4 end)[1][2] == 4)
		assert(shallowEq(smallValues, {1, 2, 3}))
		assert(shallowEq(bigValues, {4, 5, 6}))
	end

	-- Sorting operations
	do
		local tbl = {1, 2, 5, 10, 3}
		assert(not stl.is_sorted({tbl,1}, {tbl,#tbl+1}))
		assert(not stl.is_sorted({tbl,1}, {tbl,#tbl+1}, function(a,b) return a < b end))
		assert(stl.is_sorted_until({tbl,1}, {tbl,#tbl+1})[2] == 5)
		assert(stl.is_sorted_until({tbl,1}, {tbl,#tbl+1}, function(a,b) return a < b end)[2] == 5)

		stl.sort({tbl,1}, {tbl,#tbl+1})
		assert(shallowEq(tbl, {1, 2, 3, 5, 10}))

		stl.sort({tbl,1}, {tbl,#tbl+1}, function(a,b) return a > b end)
		assert(shallowEq(tbl, {10, 5, 3, 2, 1}))

		stl.partial_sort({tbl,1}, {tbl, 3}, {tbl,#tbl+1})
		assert(tbl[1] == 1 and tbl[2] == 2)

		stl.partial_sort({tbl,3}, {tbl, 5}, {tbl,#tbl+1}, function(a,b) return a > b end)
		assert(shallowEq(tbl, {1, 2, 10, 5, 3}))

		assert(stl.partial_sort_copy({tbl,1}, {tbl,4}, {tbl,4}, {tbl,6})[2] == 6)
		assert(shallowEq(tbl, {1, 2, 10, 1, 2}))

		assert(stl.partial_sort_copy({tbl,1}, {tbl,5}, {tbl,5}, {tbl,6}, function(a,b) return a < b end)[2] == 6)
		assert(shallowEq(tbl, {1, 2, 10, 1, 1}))

		stl.stable_sort({tbl,1}, {tbl,#tbl+1})
		assert(shallowEq(tbl, {1, 1, 1, 2, 10}))

		stl.stable_sort({tbl,1}, {tbl,#tbl+1}, function(a,b) return a > b end)
		assert(shallowEq(tbl, {10, 2, 1, 1, 1}))

		stl.nth_element({tbl,1}, {tbl,3}, {tbl,#tbl+1})
		assert(tbl[1] == 1 and tbl[2] == 1 and tbl[3] == 1)

		stl.nth_element({tbl,1}, {tbl,4}, {tbl,#tbl+1}, function(a,b) return a < b end)
		assert(shallowEq(tbl, {1, 1, 1, 2, 10}))
	end

	-- Binary search operations (on sorted ranges)
	do
		local tbl = {1, 2, 2, 3, 4, 5}
		assert(stl.lower_bound({tbl,1}, {tbl,#tbl+1}, 2)[2] == 2)
		assert(stl.lower_bound({tbl,1}, {tbl,#tbl+1}, 2, function(a,b) return a < b end)[2] == 2)
		assert(stl.upper_bound({tbl,1}, {tbl,#tbl+1}, 2)[2] == 4)
		assert(stl.upper_bound({tbl,1}, {tbl,#tbl+1}, 2, function(a,b) return a < b end)[2] == 4)
		assert(not stl.binary_search({tbl,1}, {tbl,#tbl+1}, 6))
		assert(not stl.binary_search({tbl,1}, {tbl,#tbl+1}, 6, function(a,b) return a < b end))
		do
			local rangeOf2 = stl.equal_range({tbl,1}, {tbl,#tbl+1}, 2)
			assert(rangeOf2[1][2] == 2 and rangeOf2[2][2] == 4)
		end
		do
			local rangeOf2 = stl.equal_range({tbl,1}, {tbl,#tbl+1}, 2, function(a,b) return a < b end)
			assert(rangeOf2[1][2] == 2 and rangeOf2[2][2] == 4)
		end
	end

	-- Other operations on sorted ranges
	do
		local tbl = {2, 4, 5, 5, 6}
		local tbl2 = {2, 3, 5, 7}
		local tbl3 = {}
		assert(stl.merge({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3,1})[2] == 10)
		assert(shallowEq(tbl3, {2, 2, 3, 4, 5, 5, 5, 6, 7}))

		assert(stl.merge({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3,1}, function(a,b) return a < b end)[2] == 10)

		local tbl4 = {1, 2, 3, 1, 2, 3}
		stl.inplace_merge({tbl4,1}, {tbl4,4}, {tbl4,#tbl+1})
		assert(shallowEq(tbl4, {1, 1, 2, 2, 3, 3}))

		tbl4 = {1, 2, 3, 1, 2, 3}
		stl.inplace_merge({tbl4,1}, {tbl4,4}, {tbl4,#tbl+1}, function(a,b) return a < b end)
		assert(shallowEq(tbl4, {1, 1, 2, 2, 3, 3}))
	end

	-- Set operations (on sorted ranges)
	do
		local tbl = {1, 2, 3, 4, 5}
		local tbl2 = {2, 5}
		assert(stl.includes({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}))
		assert(stl.includes({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, function(a,b) return a < b end))
		local tbl3 = {}
		assert(stl.set_difference({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3, 1})[2] == 4)
		assert(shallowEq(tbl3, {1, 3, 4}))

		assert(stl.set_difference({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3, 1}, function(a,b) return a < b end)[2] == 4)
		assert(stl.set_intersection({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3, 1})[2] == 3)
		assert(shallowEq(tbl3, {2, 5, 4}))

		assert(stl.set_intersection({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3, 1}, function(a,b) return a < b end)[2] == 3)

		tbl2 = {2, 5, 6}
		assert(stl.set_symmetric_difference({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3, 1})[2] == 5)
		assert(shallowEq(tbl3, {1, 3, 4, 6}))

		assert(stl.set_symmetric_difference({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3, 1}, function(a,b) return a < b end)[2] == 5)
		assert(stl.set_union({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3, 1})[2] == 7)
		assert(shallowEq(tbl3, {1, 2, 3, 4, 5, 6}))

		assert(stl.set_union({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, {tbl3, 1}, function(a,b) return a < b end)[2] == 7)
	end

	-- Heap operations
	do
		local tbl = {4, 6, 1, 2, 3, 5}
		assert(not stl.is_heap({tbl,1}, {tbl,#tbl+1}))
		assert(stl.is_heap_until({tbl,1}, {tbl,#tbl+1})[2] == 2)
		stl.make_heap({tbl,1}, {tbl,#tbl+1})
		assert(tbl[1] == 6)

		tbl[#tbl + 1] = 7
		stl.push_heap({tbl,1}, {tbl,#tbl+1})
		assert(tbl[1] == 7)

		stl.pop_heap({tbl,1}, {tbl,#tbl+1})
		assert(tbl[1] == 6 and tbl[#tbl] == 7)

		tbl[#tbl] = nil	
		stl.sort_heap({tbl,1}, {tbl,#tbl+1})
		assert(shallowEq(tbl, {1, 2, 3, 4, 5, 6}))

		local tbl2 = {4, 6, 1, 2, 3, 5}
		assert(not stl.is_heap({tbl2,1}, {tbl2,#tbl2+1}, defaultCmp))
		assert(stl.is_heap_until({tbl2,1}, {tbl2,#tbl2+1}, defaultCmp)[2] == 2)
		stl.make_heap({tbl2,1}, {tbl2,#tbl2+1}, defaultCmp)
		assert(tbl2[1] == 6)

		tbl2[#tbl2 + 1] = 7
		stl.push_heap({tbl2,1}, {tbl2,#tbl2+1}, defaultCmp)
		assert(tbl2[1] == 7)

		stl.pop_heap({tbl2,1}, {tbl2,#tbl2+1}, defaultCmp)
		assert(tbl2[1] == 6 and tbl2[#tbl2] == 7)

		tbl2[#tbl2] = nil
		stl.sort_heap({tbl2,1}, {tbl2,#tbl2+1}, defaultCmp)
		assert(shallowEq(tbl2, {1, 2, 3, 4, 5, 6}))
	end

	-- Minimum/maximum operations
	do
		local tbl = {3, 4, 2, 1}
		assert(stl.max_element({tbl,1}, {tbl,#tbl+1})[2] == 2)
		assert(stl.min_element({tbl,1}, {tbl,#tbl+1})[2] == 4)
		do
			local minmax = stl.minmax_element({tbl,1}, {tbl,#tbl+1})
			assert(minmax[1][2] == 4 and minmax[2][2] == 2)
		end
		assert(stl.clamp(10, 1, 5) == 5)

		assert(stl.max_element({tbl,1}, {tbl,#tbl+1}, defaultCmp)[2] == 2)
		assert(stl.min_element({tbl,1}, {tbl,#tbl+1}, defaultCmp)[2] == 4)
		do
			local minmax = stl.minmax_element({tbl,1}, {tbl,#tbl+1}, defaultCmp)
			assert(minmax[1][2] == 4 and minmax[2][2] == 2)
		end
		assert(stl.clamp(10, 1, 5, defaultCmp) == 5)
	end

	-- Comparison operations
	do
		local tbl = {1, 2, 3, 4, 5}
		local tbl2 = {1, 2, 4, 5}
		assert(not stl.equal({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}))
		assert(not stl.lexicographical_compare({tbl2,1}, {tbl2,#tbl2+1}, {tbl,1}, {tbl,#tbl+1}))

		assert(not stl.equal({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, defaultCmp))
		assert(not stl.lexicographical_compare({tbl2,1}, {tbl2,#tbl2+1}, {tbl,1}, {tbl,#tbl+1}, defaultCmp))
	end

	-- Permutation operations
	do
		local tbl = {2, 4, 1, 5, 4, 3}
		local tbl2 = {1, 2, 3, 4, 4, 5}
		assert(stl.is_permutation({tbl,1}, {tbl,#tbl+1}, {tbl2,1}))
		assert(stl.is_permutation({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}))
		assert(stl.next_permutation({tbl,1}, {tbl,#tbl+1}))
		assert(shallowEq(tbl, {2, 4, 3, 1, 4, 5}))
		assert(stl.prev_permutation({tbl,1}, {tbl,#tbl+1}))
		assert(shallowEq(tbl, {2, 4, 1, 5, 4, 3}))

		assert(stl.is_permutation({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, defaultEq))
		assert(stl.is_permutation({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, {tbl2,#tbl2+1}, defaultEq))
		assert(stl.next_permutation({tbl,1}, {tbl,#tbl+1}, defaultCmp))
		assert(shallowEq(tbl, {2, 4, 3, 1, 4, 5}))
		assert(stl.prev_permutation({tbl,1}, {tbl,#tbl+1}, defaultCmp))
		assert(shallowEq(tbl, {2, 4, 1, 5, 4, 3}))
	end

	-- Numeric operations
	do
		local tbl = {}
		stl.iota({tbl,1}, {tbl,11}, 1)
		assert(shallowEq(tbl, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}))

		assert(stl.accumulate({tbl,1}, {tbl,#tbl+1}, 0) == 55)
		assert(stl.accumulate({tbl,1}, {tbl,#tbl+1}, 1, function(a,b) return a*b end) == 3628800)


		local tbl2 = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2}
		assert(stl.inner_product({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, 0) == 110)
		assert(stl.inner_product({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, 0, function(a,b) return a+b end, function(a,b) return a*b end) == 110)

		assert(stl.adjacent_difference({tbl,1}, {tbl,#tbl+1}, {tbl,1})[2] == 11)
		assert(shallowEq(tbl, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}))

		assert(stl.partial_sum({tbl,1}, {tbl,#tbl+1}, {tbl,1})[2] == 11)
		assert(shallowEq(tbl, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}))

		assert(stl.adjacent_difference({tbl,1}, {tbl,#tbl+1}, {tbl,1}, function(a,b) return a-b end)[2] == 11)
		assert(shallowEq(tbl, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}))

		assert(stl.partial_sum({tbl,1}, {tbl,#tbl+1}, {tbl,1}, function(a,b) return a+b end)[2] == 11)
		assert(shallowEq(tbl, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}))

		assert(stl.reduce({tbl,1}, {tbl,#tbl+1}, 10) == 65)
		assert(stl.reduce({tbl,1}, {tbl,#tbl+1}, 1, function(a,b) return a*b end) == 3628800)

		local tbl2 = {}
		assert(stl.inclusive_scan({tbl,1}, {tbl,#tbl+1}, {tbl2,1})[2] == 11)
		assert(shallowEq(tbl2, {1, 3, 6, 10, 15, 21, 28, 36, 45, 55}))

		assert(stl.inclusive_scan({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, function(a,b) return a+b end)[2] == 11)
		assert(stl.inclusive_scan({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, function(a,b) return a+b end, 0)[2] == 11)
		assert(stl.exclusive_scan({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, 0)[2] == 11)
		assert(shallowEq(tbl2, {0, 1, 3, 6, 10, 15, 21, 28, 36, 45}))

		assert(stl.exclusive_scan({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, 0, function(a,b) return a+b end)[2] == 11)

		tbl2 = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2}
		assert(stl.transform_reduce({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, 0) == 110)
		assert(stl.transform_reduce({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, 0, function(a,b) return a+b end, function(a,b) return a*b end) == 110)
		assert(stl.transform_reduce({tbl,1}, {tbl,#tbl+1}, 0, function(a,b) return a+b end, function(v) return v*2 end) == 110)
		assert(stl.transform_exclusive_scan({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, 0, function(a,b) return a+b end, function(v) return v*2 end)[2] == 11)
		assert(shallowEq(tbl2, {0, 2, 6, 12, 20, 30, 42, 56, 72, 90}))

		assert(stl.transform_inclusive_scan({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, function(a,b) return a+b end, function(v) return v*2 end)[2] == 11)
		assert(shallowEq(tbl2, {2, 6, 12, 20, 30, 42, 56, 72, 90, 110}))

		assert(stl.transform_inclusive_scan({tbl,1}, {tbl,#tbl+1}, {tbl2,1}, function(a,b) return a+b end, function(v) return v*2 end, 0)[2] == 11)		
	end
end
