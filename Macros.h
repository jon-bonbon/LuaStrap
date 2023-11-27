#pragma once

#define lstrapTraits(typeName) \
	template <> \
	struct LuaStrap::Traits<typeName> { \
		using Type = typeName; \
		inline static auto members = std::tuple{

#define lstrapAggrTraits(typeName) \
	template <> \
	struct LuaStrap::Traits<typeName> : LuaStrap::AggregateTraits<typeName> { \
		using Type = typeName; \
		inline static auto members = std::tuple{

#define lstrapMem(memName) std::pair{ #memName, &Type::memName }

#define lstrapTraitsEnd }; };

// Example usage (using the Aggregates example from the readme)
/*
lstrapAggrTraits(Person)
	lstrapMem(name),
	lstrapMem(address),
	lstrapMem(age),
	lstrapMem(isAdult),
	lstrapMem(isHomeless)
lstrapTraitsEnd
*/