#pragma once
#include "CppLuaInterface.h"
#include "Helpers.h"
#include <algorithm>
#include <vector>

#define lstrapFuncWrapper(funcName) []<typename... Ts>(Ts&... a) requires requires { funcName(a...); } { return funcName(a...); }

namespace LuaStrap {
	template <typename Continuation, typename NextArg>
	constexpr auto leadsAnywhere() {
		return std::same_as < std::true_type, std::decay_t<decltype(
			std::declval<Continuation>()((NextArg*)nullptr)
		)>>;
	}

	template <typename Continuation, typename... NextArgs>
	constexpr auto anyLeadAnywhere(std::tuple<NextArgs...>*) {
		return (leadsAnywhere<Continuation, NextArgs> || ...);
	}

	inline struct {} leadsNowhereErr;
	inline struct {} wrongFormatErr;

	// A builder which successively tries to read a lua value as any one of Ts until success or exhaustion
	template <typename... Ts>
	struct SimpleBuilder {
		static_assert(sizeof...(Ts) > 0);
		using PossibleTypes = std::tuple<Ts...>;

		template <typename Continuation, typename Pool>
		void operator()(Continuation continuation, Pool& pool, lua_State* ls, int idx) const {
			auto howManyLeadAnywhere = 0;
			auto tryRead = [&]<typename T>() {
				if constexpr (LuaStrap::leadsAnywhere<Continuation, T>()) {
					++howManyLeadAnywhere;
					auto val = LuaStrap::read<T>(ls, idx);
					if (val) {
						continuation(pool.template build<T>(std::move(*val)));
						return true;
					}
				}
				return false;
			};
			auto dummy =
				(tryRead.template operator() < Ts > () || ...) ||
				((howManyLeadAnywhere == 0) ? continuation(leadsNowhereErr) : continuation(wrongFormatErr), false)
				;
		}
	};
	template <typename... Ts>
	struct SimpleAmbiguousBuilder : SimpleBuilder<Ts...> {
		constexpr static bool ambiguous = true;
	};

	template <typename... Builders>
	using PoolForBuilders = Pool<
		sizeof...(Builders),
		(integerCeil(
			mostSpaciousTupleElem<alignof(std::max_align_t)>((typename Builders::PossibleTypes*)nullptr),
			alignof(std::max_align_t)
		) + ...),
		alignof(std::max_align_t)
	>;

	// Part of a recursive chain. Invokes the builder for the next parameter, and incorporates the result into
	// 'ArgsSoFar' for the next bulkExecStep. Once all builders were invoked, 'Exec' is called with the arguments thus accumulated.
	// Returns true_type if, given 'ArgsSoFar', a valid call to 'Exec' can be made with some further arguments.
	// Otherwise returns false_type, meaning that calling 'Exec' is already impossible.
	// This discrimination happens at compile-time.
	template <typename Exec, typename... Builders, typename... ArgsSoFar>
	auto bulkExecStep(std::tuple<ArgsSoFar...>*, lua_State* ls, int idx, PoolForBuilders<Builders...>& pool, Exec ex, std::tuple<Builders...>, int* argOrder) {
		lua_checkstack(ls, 1);	// for a potential error message

		// If all args were built, prepare for calling
		if constexpr (sizeof...(ArgsSoFar) == sizeof...(Builders))
		{
			if constexpr (std::invocable<const Exec, ArgsSoFar&...>)
			{
				using ResultType = std::invoke_result_t<const Exec, ArgsSoFar&...>;
				static_assert(std::same_as<ResultType, void> || LuaStrap::LuaInterfacable<std::decay_t<ResultType>>,
					"Resulting type of pushed func is not writable to lua.");

				auto argPtrs = pool.getElemPtrs();

				auto caller = [&](ArgsSoFar&... args)
				{
					lua_checkstack(ls, 2);
					if constexpr (std::same_as<ResultType, void>) {
						ex(args...);
						lua_pushinteger(ls, 0);	// the amount of return values
					}
					else {
						LuaStrap::write(ls, ex(args...));
						lua_pushinteger(ls, 1);	// the amount of return values
					}

					auto emplaceIfPossible = [](lua_State* ls, const auto& val, int idx) {
						if constexpr (requires{ LuaStrap::emplace(ls, val, idx); }) {
							LuaStrap::emplace(ls, val, idx);
						}
					};
					auto argIdx = 1;
					int dummy[] = {
						(emplaceIfPossible(ls, args, argIdx++), 0)...
					};
				};

				static_assert(sizeof...(ArgsSoFar) <= 8);

				#define arg(n) *static_cast<std::tuple_element_t<n, std::tuple<ArgsSoFar...>>*>(static_cast<void*>(argPtrs[n]))
				if constexpr (sizeof...(ArgsSoFar) == 1)		caller(arg(0));
				else if constexpr (sizeof...(ArgsSoFar) == 2)	caller(arg(0), arg(1));
				else if constexpr (sizeof...(ArgsSoFar) == 3)	caller(arg(0), arg(1), arg(2));
				else if constexpr (sizeof...(ArgsSoFar) == 4)	caller(arg(0), arg(1), arg(2), arg(3));
				else if constexpr (sizeof...(ArgsSoFar) == 5)	caller(arg(0), arg(1), arg(2), arg(3), arg(4));
				else if constexpr (sizeof...(ArgsSoFar) == 6)	caller(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
				else if constexpr (sizeof...(ArgsSoFar) == 7)	caller(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5), arg(6));
				else if constexpr (sizeof...(ArgsSoFar) == 8)	caller(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5), arg(6), arg(7));
				#undef arg

				pool.template destruct<ArgsSoFar...>();
				return std::true_type{};
			}
			else {
				pool.template destruct<ArgsSoFar...>();
				lua_pushstring(ls, noMatchingOverloadError<ArgsSoFar...>(ls, argOrder, "No overload available for the given arguments:").c_str());
				return std::false_type{};
			}
		}

		// Otherwise, keep building
		else
		{
			using NextBuilder = std::tuple_element_t<sizeof...(ArgsSoFar), std::tuple<Builders...>>;

			constexpr auto doesLeadAnywhere = []<typename... PossibleTypes>(std::tuple<PossibleTypes...>*) constexpr {
				auto nextStepReturnTypePtr = []<typename NextType>() constexpr {
					return (decltype(bulkExecStep(
						(std::tuple<ArgsSoFar..., NextType>*)nullptr,
						std::declval<lua_State*>(), std::declval<int>(), std::declval<PoolForBuilders<Builders...>&>(), std::declval<Exec>(),
						std::declval<std::tuple<Builders...>>(), std::declval<int*>()
					))*)nullptr;
				};
				return (std::is_same_v<std::true_type, std::decay_t<decltype(*nextStepReturnTypePtr.template operator() < PossibleTypes > ())>> || ...);
			};
			if constexpr (!doesLeadAnywhere((typename NextBuilder::PossibleTypes*)nullptr)) {
				pool.template destruct<ArgsSoFar...>();
				lua_pushstring(ls, noMatchingOverloadError<ArgsSoFar...>(ls, argOrder, "No overload available for the given partial list of arguments:").c_str());
				return std::false_type{};
			}
			else {
				auto nextStep = [&]<typename NewestArg>(NewestArg) requires
					(std::is_pointer_v<NewestArg> && !std::is_const_v<NewestArg>)
					|| std::same_as<NewestArg, std::nullopt_t>
					|| std::same_as<NewestArg, decltype(leadsNowhereErr)>
					|| std::same_as<NewestArg, decltype(wrongFormatErr)>
				{
					if constexpr (std::same_as<NewestArg, std::nullopt_t>) {
						lua_pushstring(ls, failedToReadError<NextBuilder>(ls, argOrder[sizeof...(ArgsSoFar)] + 1).c_str());
					}
					else if constexpr (std::same_as<NewestArg, decltype(leadsNowhereErr)>) {
						lua_pushstring(ls, noMatchingOverloadError<ArgsSoFar...>(ls, argOrder, "No overload available for the given arguments:").c_str());
					}
					else if constexpr (std::same_as<NewestArg, decltype(wrongFormatErr)>) {
						lua_pushstring(ls, failedToReadError<NextBuilder>(ls, argOrder[sizeof...(ArgsSoFar)] + 1).c_str());
					}
					else {
						using Tupl = std::tuple<ArgsSoFar..., std::remove_pointer_t<NewestArg>>;
						return bulkExecStep((Tupl*)nullptr, ls, idx + 1, pool, ex, std::tuple<Builders...>{}, argOrder);
					}
				};
				NextBuilder{}(nextStep, pool, ls, idx);
				return std::true_type{};
			}
		}
	}

	template <typename T>
	struct BuilderPartitionPred : std::true_type {};
	template <typename T> requires requires { T::ambiguous; }
	struct BuilderPartitionPred<T> : std::false_type {};

	// Wraps a generic c++ invocable (of format [-0, +n, m]) such that it can be called from lua with different sets of arguments
	// (see bulkExecStep), and pushes it on stack top.
	template <typename... Builders, typename Exec>
	void pushBulkFunc(lua_State* ls, Exec)			// [-0, +1, m]
	{
		// Reorder builders so that the ambiguous ones come last.
		// This makes for smarter "type deduction" in the arg reading process.
		using Par = TypePartition<BuilderPartitionPred, Builders...>;
		using BuilderOrder = typename Par::first_type;
		using BuilderInverseOrder = typename Par::second_type;

		static auto reorderedExec =
			[]<typename... ReArgs>(ReArgs&&... reorderedArgs)
			requires (invocableOutOfOrder<Exec, ReArgs...>(BuilderInverseOrder{}))
		{
			auto argsTuple = std::tuple<ReArgs&&...>{ reorderedArgs... };
			return[&]<int... Indices>(std::integer_sequence<int, Indices...>) {
				return Exec{}(get<Indices>(argsTuple)...);
			}(typename Par::second_type{});
		};

		using BuildersTuple = std::tuple<Builders...>;
		using ReorderedBuildersTuple = ReorderedTuple<BuildersTuple, BuilderOrder>;
		using Pool = UnwrapTuple<PoolForBuilders, ReorderedBuildersTuple>;

		static_assert(std::convertible_to<std::true_type, decltype(bulkExecStep(
			std::declval<std::tuple<>*>(), ls, 1, std::declval<Pool&>(), reorderedExec, ReorderedBuildersTuple{}, std::declval<int*>()
		))>, "The supplied function cannot be called with the supplied builders (no combination of their arguments is valid).");

		lua_pushcfunction(ls, [](lua_State* ls) {
			lua_checkstack(ls, sizeof...(Builders));
			lua_settop(ls, sizeof...(Builders));
			reorderLuaStack(ls, BuilderOrder{});

			auto argOrder = unwrapIntegerSequence<std::vector<int>>(BuilderOrder{});
			Pool pool;
			bulkExecStep((std::tuple<>*)nullptr, ls, 1, pool, reorderedExec, ReorderedBuildersTuple{}, argOrder.data());
			
			// elements of 'pool' have now been destructed by bulkExecStep

			if (lua_isinteger(ls, -1)) {
				int retCount = lua_tointeger(ls, -1);
				lua_pop(ls, 1);
				return retCount;
			}
			else {
				lua_checkstack(ls, 1);
				return luaL_error(ls, lua_tostring(ls, -1));
			}
		});
	}
}
