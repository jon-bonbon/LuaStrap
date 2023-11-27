#include "Tests.h"
#include "../LuaStrap.h"
#include <map>
#include <variant>
#include <string>
#include <vector>
#include <array>

// Item 1 - Functions of basic types (built in types + standard containers)
auto average(double a, double b) {
	return (a + b) / 2.0;
}
void eraseKey(
	std::map <
	std::variant<std::string, int>,
	std::vector<double>
	>& data,
	std::variant<std::string, int> key
) {
	data.erase(key);
}

// Item 2 - Overloaded/generic functions
auto plus(double lhs, double rhs) { return lhs + rhs; }
auto plus(std::string lhs, std::string rhs) { return lhs + rhs; }
auto plus(auto lhs, auto rhs) { return lhs + rhs; }		// generic function taking anything

// Item 3 - Aggregates
struct Person {
	std::string name;
	std::string address;
	int age;

	auto isAdult() const { return age >= 18; }
	auto isHomeless() const { return address == ""; }
};
void mature(Person& p) { if (p.age < 18) { p.age = 18; } }

template <>
struct LuaStrap::Traits<Person> : LuaStrap::AggregateTraits<Person> {
	inline static auto members = std::tuple{
		std::pair{ "name", &Person::name },
		std::pair{ "address", &Person::address },
		std::pair{ "age", &Person::age },
		std::pair{ "isAdult", &Person::isAdult },
		std::pair{ "isHomeless", &Person::isHomeless }
	};
};

/* Macro syntax:
lstrapAggrTraits(Person)
	lstrapMem(name),
	lstrapMem(address),
	lstrapMem(age),
	lstrapMem(isAdult),
	lstrapMem(isHomeless)
lstrapTraitsEnd
*/

// Item 4 - Baking lua data
using PointCloud = std::vector<std::array<float, 3>>;
void process(const PointCloud& pcloud) {
	// ...
}

// Item 5 - Complex classes
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
struct LuaStrap::Traits<Scene> {
	inline static auto members = std::tuple{
		std::pair{ "render", &Scene::render },
		std::pair{ "getObjCount", &Scene::getObjCount },
		std::pair{ "clearAllObjects", &Scene::clearAllObjects }
	};
};
// ^ by omitting "AggregateTraits", we only allow Scene to be represented as baked data

/* Macro syntax:
lstrapTraits(Scene)
	lstrapMem(render),
	lstrapMem(getObjCount),
	lstrapMem(clearAllObjects)
lstrapTraitsEnd
*/

void doLuaTests(lua_State* ls) {
	namespace lst = LuaStrap;

	// Publish baking mechanisms into the global table (not required)
	lua_geti(ls, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	lst::publishLuaStrapUtils(ls);
	lua_pop(ls, 1);

	// Publish STL algos into a custom table (not required)
	lua_createtable(ls, 0, 0);
	lst::publishStl(ls);
	lua_setglobal(ls, "stl");

	// Item 1
	lst::pushFunc(ls, average);
	lua_setglobal(ls, "average");
	lst::pushFunc(ls, eraseKey);
	lua_setglobal(ls, "eraseKey");

	// Item 2
	lst::pushOverloadedFunc(ls,
		(double(*)(double, double)) & plus,
		(std::string(*)(std::string, std::string)) & plus,
		(std::complex<double>(*)(std::complex<double>, std::complex<double>)) & plus,
		(std::complex<double>(*)(std::complex<double>, double)) & plus,
		(std::complex<double>(*)(double, std::complex<double>)) & plus
		// ...
	);
	lua_setglobal(ls, "plus");

	// Item 3
	lst::pushFunc(ls, +[] { return Person{}; });
	lua_setglobal(ls, "makePerson");
	// ^ providing a factory function is NOT neccessary, but useful, see the lua code
	lst::pushFunc(ls, mature);
	lua_setglobal(ls, "mature");

	// Item 4
	lst::pushFunc(ls, process);
	lua_setglobal(ls, "process");

	// Item 5
	lst::pushFunc(ls, lst::makeBakedData<Scene, int>);
	lua_setglobal(ls, "makeScene");
	// ^ providing a factory function IS neccessary, since this type has no lua representation


	luaL_dostring(ls, R"delim(

	-- Item 1
	assert( average(3, 5) == 4 )
	local tbl = {
		["abcd"] = {1.1, 2.2, 3.3},
		["efg"] = {},
		[50] = {10.0, 11.0}
	}
	eraseKey(tbl, "abcd")
	eraseKey(tbl, 50)
	assert( tbl["abcd"] == nil and tbl["efg"] == {} and tbl[50] == nil )

	-- Item 2
	assert( plus(10, 5) == 15 )
	assert( plus("Hello, ", "world!") == "Hello, world!" )
	local sum1 = plus({1, 2}, {2, 3})		assert( sum1[1] == 3 and sum1[2] == 5 )
	local sum2 = plus({1, 2}, 2)			assert( sum2[1] == 3 and sum2[2] == 2 )
	local sum3 = plus(2, {1, 2})			assert( sum3[1] == 3 and sum3[2] == 2 )

	-- Item 3
	local p = makePerson()
	p.name = "Bradley"; p.age = 17; p.address = ""
	assert( p:isHomeless() and not p:isAdult() )
	mature(p)
	assert( p:isAdult() )

	local p2 = { name = "Anna", age = 15, address = "" }	-- If desired, aggregates can be created directly, without using a factory function.
	mature(p2)												-- Free functions can be called over these...
	--p2:isAdult()	-- Error!!								-- ...but member functions can not.

	-- Item 4
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

	-- Item 5
	local sc = makeScene(10)
	assert( sc:getObjCount() == 10 )
	sc:render(640, 480)
	sc:clearAllObjects()
	assert( sc:getObjCount() == 0 )

	)delim");



}
