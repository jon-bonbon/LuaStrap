# LuaStrap
A library for binding C++ functions and types to Lua, with a focus on generic programming. To that end, it provides a mechanism for binding families of functions operating on a generic concept, and makes available the entirety of the STL. In addition, most of the features expected of a lua binding library are provided. LuaStrap does thorough runtime error checking and uses C++20 concepts and constraints for high-quality compiler errors.

# Initialization
C++20 and Lua 5.3 are required. LuaStrap.cpp must be compiled as part of the project.
```c++
#include "LuaStrap/LuaStrap.h"

//...

namespace lst = LuaStrap;

// Publish baking mechanisms into the global table (not required)
lua_geti(ls, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
lst::publishLuaStrapUtils(ls);
lua_pop(ls, 1);

// Publish STL algos into a custom table (not required)
lua_createtable(ls, 0, 0);
lst::publishStl(ls);
lua_setglobal(ls, "stl");
```

# Functions of basic types (built in types + standard containers)
```c++
auto average(double a, double b) {
	return (a + b) / 2.0;
}

// Later...
lst::pushFunc(ls, average);
lua_setglobal(ls, "average");

///////////////////////////////////

void eraseKey(
	std::map <
		std::variant<std::string, int>,
		std::vector<double>
	>& data,
	std::variant<std::string, int> key
)
{
	data.erase(key);
}

// Later:
lst::pushFunc(ls, eraseKey);
lua_setglobal(ls, "eraseKey");
```
```lua
assert( average(3, 5) == 4 )
local tbl = {
	["abcd"] = {1.1, 2.2, 3.3},
	["efg"] = {},
	[50] = {10.0, 11.0}
}
eraseKey(tbl, "abcd")
eraseKey(tbl, 50)
assert( tbl["abcd"] == nil and tbl["efg"] == {} and tbl[50] == nil )
```

# Overloaded/generic functions
```c++
auto plus(double lhs, double rhs) { return lhs + rhs; }
auto plus(std::string lhs, std::string rhs) { return lhs + rhs; }
auto plus(auto lhs, auto rhs) { return lhs + rhs; }		// generic function taking anything

// Later...
lst::pushOverloadedFunc(ls,
	(double(*)(double, double)) & plus,
	(std::string(*)(std::string, std::string)) & plus,
	(std::complex<double>(*)(std::complex<double>, std::complex<double>)) & plus,
	(std::complex<double>(*)(std::complex<double>, double)) & plus,
	(std::complex<double>(*)(double, std::complex<double>)) & plus
	// ...
);
lua_setglobal(ls, "plus");

```
```lua
assert( plus(10, 5) == 15 )
assert( plus("Hello, ", "world!") == "Hello, world!" )
local sum1 = plus({1, 2}, {2, 3})		assert( sum1[1] == 3 and sum1[2] == 5 )
local sum2 = plus({1, 2}, 2)			assert( sum2[1] == 3 and sum2[2] == 2 )
local sum3 = plus(2, {1, 2})			assert( sum3[1] == 3 and sum3[2] == 2 )
```
For a more sophisticated way to bind generic functions, see "Case study - mathematical vectors and matrices"

# Aggregates
```c++
struct Person {
	std::string name;
	std::string address;
	int age;

	auto isAdult() const { return age >= 18; }
	auto isHomeless() const { return address == ""; }
};
void mature(Person& p) { if (p.age < 18) { p.age = 18; } }

template <>
struct LuaBinding::Traits<Person> : LuaBinding::AggregateTraits<Person> {
	inline static auto members = std::tuple{
		std::pair{ "name", &Person::name },
		std::pair{ "address", &Person::address },
		std::pair{ "age", &Person::age },
		std::pair{ "isAdult", &Person::isAdult },
		std::pair{ "isHomeless", &Person::isHomeless }
	};
};

// Later...
lst::pushFunc(ls, +[] { return Person{}; });
lua_setglobal(ls, "makePerson");
// ^ providing a factory function is NOT neccessary, but useful, see the lua code
lst::pushFunc(ls, mature);
lua_setglobal(ls, "mature");

```
```lua
local p = makePerson()
p.name = "Bradley"; p.age = 17; p.address = ""
assert( p:isHomeless() and not p:isAdult() )
mature(p)
assert( p:isAdult() )

local p2 = { name = "Anna", age = 15, address = "" }   -- If desired, aggregates can be created directly, without using a factory function.
mature(p2)                                             -- Free functions can be called over these...
--p2:isAdult()  -- Error!!                             -- ...but member functions cannot.
```
The trait syntax is not ideal, but is the best that C++ allows. For users not afraid of macro use, the file Macros.h provides a much better syntax.

# Baking lua data
```c++
using PointCloud = std::vector<std::array<float, 3>>;
void process(const PointCloud& pcloud) {
	// ...
}

// Later...
lst::pushFunc(ls, process);
lua_setglobal(ls, "process");

```
```lua
local pointCloud = { {0,1,0}, {2.5, 1, 0.5}, --[[ ... very much data ]] }

-- Each of these invocations converts 'pointCloud' from its
-- lua format to its c++ format - inefficient for such large data.
process(pointCloud)
process(pointCloud)
process(pointCloud)
-- ...

pointCloud = markedForBaking(pointCloud)

-- Now only the first call does a conversion - after that, 'pointCloud'
-- represents fully baked c++ data.
process(pointCloud)
process(pointCloud)
-- ...
-- pointCloud[1] = {0,2,0}	-- Error!! Can't directly touch baked data, can only call functions on them (both free and member funcs)

-- If needed later, the data can be translated back to the lua format
pointCloud = unbaked(pointCloud)
pointCloud[1] = {0,2,0}		-- Now ok!
```

# Complex classes
```c++
class Scene {
public:
	Scene(int objCount) : objCount{ objCount } { /* ... */ }
	void render(int resX, int resY) const { /* ... */ }
	auto getObjCount() const { return objCount; }
	void clearAllObjects() { objCount = 0; /* ... */ }
	// ...
private:
	int objCount;
	// ... a million other things ...
};
template <>
struct LuaBinding::Traits<Scene> {
	inline static auto members = std::tuple{
		std::pair{ "render", &Scene::render },
		std::pair{ "getObjCount", &Scene::getObjCount },
		std::pair{ "clearAllObjects", &Scene::clearAllObjects }
	};
};
// ^ by omitting "AggregateTraits", we only allow Scene to be represented as baked data

// Later...
lst::pushFunc(ls, lst::makeBakedData<Scene, int>);
lua_setglobal(ls, "makeScene");
// ^ providing a factory function IS neccessary, since this type has no lua representation

```
```lua
local sc = makeScene(10)
assert( sc:getObjCount() == 10 )
sc:render(640, 480)
sc:clearAllObjects()
assert( sc:getObjCount() == 0 )
```

# Case study - mathematical vectors and matrices
If desiring to bind a generic library, manually enumerating the entire supported overload set for each of the functions would be tedious, and possibly problematic for runtime performance. This section demonstrates a better way.
An example generic library 'VecMat' is assumed, see VectorMatrixTest.cpp for its specification.
```c++
// We specify a builder type for each 'parameter kind' used in the library, representing the set of types it can stand for
using floatB = lst::SimpleBuilder<float>;
using vecB = lst::SimpleBuilder<VecMat::Vec<float, 2>, VecMat::Vec<float, 3>, VecMat::Vec<float, 4>>;
using matB = lst::SimpleAmbiguousBuilder<
	VecMat::Mat<float, 2, 2>, VecMat::Mat<float, 2, 3>, VecMat::Mat<float, 2, 4>,
	VecMat::Mat<float, 3, 2>, VecMat::Mat<float, 3, 3>, VecMat::Mat<float, 3, 4>,
	VecMat::Mat<float, 4, 2>, VecMat::Mat<float, 4, 3>, VecMat::Mat<float, 4, 4>
>;
// ^ we mark this builder ambiguous, since a single lua datum can correspond to multiple types
//  (i.e. a lua array of 6 elems can be a 2*3 matrix, or a 3*2 matrix)

lst::pushBulkFunc<vecB, vecB>               // < we specify the function's args - 'dot' takes two vectors
    (ls, lstrapFuncWrapper(VecMat::dot));   // < since 'dot' is a template, not a true function, we must wrap it
lua_setglobal(ls, "dot");

lst::pushBulkFunc<vecB>(ls, lstrapFuncWrapper(VecMat::length));
lua_setglobal(ls, "length");

lst::pushBulkFunc<vecB, vecB, floatB>(ls, lstrapFuncWrapper(VecMat::scaleAlong));
lua_setglobal(ls, "scaleAlong");

lst::pushBulkFunc<vecB, vecB>(ls, lstrapFuncWrapper(VecMat::cross));
lua_setglobal(ls, "cross");

lst::pushBulkFunc<vecB>(ls, lstrapFuncWrapper(VecMat::normalize));
lua_setglobal(ls, "normalize");

lst::pushBulkFunc<vecB, vecB>(ls, lstrapFuncWrapper(VecMat::operator+=));
lua_setglobal(ls, "vecMutAdd");

lst::pushBulkFunc<matB, vecB>(ls, lstrapFuncWrapper(VecMat::operator*));
lua_setglobal(ls, "matVecMul");

lst::pushBulkFunc<matB, matB>(ls, lstrapFuncWrapper(VecMat::operator*));
lua_setglobal(ls, "matMatMul");

```
```lua
assert(dot({0, 1, 0, 1}, {1.0, 0.75, 0.5, 0.25}) == 1.0)
--dot({0, 1, 0, 1}, {1.0, 0.75, 0.5})		-- error! can't dot a 4-vector with a 3-vector

local cr = cross({1, 0, 0}, {0, 1, 0})
assert(cr[1] == 0 and cr[2] == 0 and cr[3] == 1)

local sum = {1, 2}
vecMutAdd(sum, {2, 3})	
assert(sum[1] == 3 and sum[2] == 5)

scaleAlong(sum, {1, 0}, 2)
assert(sum[1] == 6 and sum[2] == 5)

normalize(sum)
assert(length(sum) < 1.01)

local m = {
	1, 0,
	0, 1,
	1, 1
}

-- m is interpreted as a 2x3 matrix, to match the 2-vector
local v = { 2, 3 }
local newV = matVecMul(m, v)
assert(newV[1] == 2 and newV[2] == 3 and newV[3] == 5)

-- m is interpreted as a 3x2 matrix, to match the 3-vector
local v = { 2, 3, 4 }
local newV = matVecMul(m, v)
assert(newV[1] == 2 and newV[2] == 9)

local mat = {
	3, 5, 7,
	7, 5, 3,
	5, 7, 3
}
local scaleMat = {
	2, 0, 0,
	0, 2, 0,
	0, 0, 2
}

local doubledMat = matMatMul(scaleMat, mat)

-- Two possible interpretations; either mat1 is 2x3 and mat2 is 3x2, or the other way around.
-- Which one wins is deterministic, but not immediately obvious. Avoid this.
local badMat1 = {
	3, 5,
	7, 5,
	3, 7
}
local badMat2 = {
	7, 3, 5,
	5, 7, 3
}
local confusingMat = matMatMul(scaleMat, mat)
```

In case the performance of 'SimpleBuilders' is unsatisfactory (which is not likely), the user can define their own builders - see VectorMatrixTest.cpp for an example.








