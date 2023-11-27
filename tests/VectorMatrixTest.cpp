#include "Tests.h"
#include "../GenericFuncBinding.h"
#include "../BasicTraits.h"
#include <cmath>
#include <array>

namespace VecMat {

	// An example collection of generic functions over arbitrary vectors/matrices.  
	// The test will bind this collection to lua and try to use it.

	template <typename T>
	struct VectorTraits {}; // specialize this for custom vector classes
	template <typename T>
	concept Vector =
		requires(T & t, const T & ct, const typename VectorTraits<T>::ElemType & elm) {
			{ VectorTraits<T>::dimension } -> std::convertible_to<int>;
			{ VectorTraits<T>::getElem(ct, int{}) } -> std::convertible_to<typename VectorTraits<T>::ElemType>;
			{ VectorTraits<T>::setElem(t, int{}, elm) };
	}&&
		VectorTraits<std::decay_t<T>>::dimension > 0;

	template <typename T, int howManyElems> struct Vec { std::array<T, howManyElems> elems; };
	template <typename T, int howManyElems> struct VectorTraits<Vec<T, howManyElems>> {
		using ElemType = T;
		constexpr static int dimension = howManyElems;
		constexpr static auto getElem(const Vec<T, howManyElems>& vec, int idx) -> const T& { return vec.elems[idx]; }
		constexpr static void setElem(Vec<T, howManyElems>& vec, int idx, const T& val) { vec.elems[idx] = val; }
	};
	template <Vector V>
	struct LuaStrap::Traits<V> {
		// ElemT must not push on read
		// { [1] = elm1, ... }
		using VecTraits = VecMat::VectorTraits<V>;
		using ElemT = std::remove_cvref_t < std::invoke_result_t<decltype([](const V& v, int i) { return VecTraits::getElem(v, i); }), V, int >> ;
		static auto read(lua_State* ls, int idx) -> std::optional<V> {
			auto res = std::optional<V>{ std::in_place };
			auto howManyRead = LuaStrap::readArrayUpTo<ElemT>(ls, idx, VecTraits::dimension, [&res, elmI = 0](const ElemT& elem) mutable {
				VecTraits::setElem(*res, elmI++, elem);
				});
			if (howManyRead == VecTraits::dimension) {
				return res;
			}
			else {
				return std::nullopt;
			}
		}
		static void emplace(lua_State* ls, const V& v, int absIdx) {
			for (auto i = 0; i < VecTraits::dimension; ++i) {
				LuaStrap::write<ElemT>(ls, VecTraits::getElem(v, i));
				lua_seti(ls, absIdx, i + 1);
			}
		}
	};

	template <typename T>
	struct MatrixTraits {};	// specialize this for custom matrix classes
	template <typename T>
	concept Matrix =
		requires(T & t, const T & ct, const typename MatrixTraits<T>::Elem & elm) {
			{ MatrixTraits<T>::dimension.x } -> std::convertible_to<int>;
			{ MatrixTraits<T>::dimension.y } -> std::convertible_to<int>;
			{ MatrixTraits<T>::getElem(ct, int{}, int{}) } -> std::convertible_to<typename MatrixTraits<T>::Elem>;
			{ MatrixTraits<T>::setElem(t, int{}, int{}, elm) };
			// +x goes right, +y goes down!
	}&&
		MatrixTraits<T>::dimension.x > 0 &&
		MatrixTraits<T>::dimension.y > 0;

	template <typename T, int w, int h> struct Mat { std::array<T, w* h> elems; };
	template <typename T, int w, int h> struct MatrixTraits<Mat<T, w, h>> {
		using Elem = T;
		constexpr static Vec<int, 2> dimension = { w, h };

		constexpr static auto getElem(const Mat<T, w, h>& mat, int i) -> const T& { return mat.elems[i]; }
		constexpr static void setElem(Mat<T, w, h>& mat, int i, const T& val) { mat.elems[i] = val; }
	};
	template <Matrix M>
	struct LuaStrap::Traits<M> {
		// ElemT must not push on read
		// { [1] = elm1, ... }
		using MatTraits = VecMat::MatrixTraits<M>;
		using ElemT = MatTraits::Elem;
		constexpr static auto matSize = MatTraits::dimension.x * MatTraits::dimension.y;

		static auto read(lua_State* ls, int idx) -> std::optional<M> {
			if (lua_type(ls, idx) != LUA_TTABLE) {
				return std::nullopt;
			}

			M res;
			auto readCount = LuaStrap::readArrayUpTo<ElemT>(ls, idx, matSize, [&res, i = 0](const ElemT& val) mutable {
				MatTraits::setElem(res, i, val);
				++i;
				});

			if (readCount == matSize) {
				return std::optional{ res };
			}
			else {
				return std::nullopt;
			}
		}
		static void emplace(lua_State* ls, const M& v, int idx) {
			for (auto i = 0; i < matSize; ++i) {
				LuaStrap::write(ls, MatTraits::getElem(v, i));
				lua_seti(ls, idx, i + 1);
			}
		}
	};

	template <Vector V>
	constexpr auto lengthSqr(const V& v) {
		auto sum = VectorTraits<V>::getElem(v, 0) * VectorTraits<V>::getElem(v, 0);
		for (auto i = 1; i < VectorTraits<V>::dimension; ++i) {
			sum += VectorTraits<V>::getElem(v, i) * VectorTraits<V>::getElem(v, i);
		}
		return sum;
	}

	template <Vector V>
	constexpr auto length(const V& v) {
		return std::sqrt(lengthSqr(v));
	}

	template <Vector VecA, Vector VecB>
	constexpr auto scaleAlong(VecA& a, const VecB& axis, typename VectorTraits<VecA>::ElemType sc)
		requires (VectorTraits<VecA>::dimension == VectorTraits<VecB>::dimension)
	{
		auto d = dot(a, axis);
		for (auto index = 0; index < VectorTraits<VecA>::dimension; ++index) {
			VectorTraits<VecA>::setElem(a, index, VectorTraits<VecA>::getElem(a, index) + VectorTraits<VecB>::getElem(axis, index) * d * (sc - 1));
		}
	}

	template <Vector VecA, Vector VecB>
	constexpr auto dot(const VecA& a, const VecB& b)
		requires (VectorTraits<VecA>::dimension == VectorTraits<VecB>::dimension)
	{
		auto result = decltype(VectorTraits<VecA>::getElem(a, 0) * VectorTraits<VecB>::getElem(b, 0)){0};
		for (auto index = 0; index < VectorTraits<VecA>::dimension; ++index) {
			result += VectorTraits<VecA>::getElem(a, index) * VectorTraits<VecB>::getElem(b, index);
		}
		return result;
	}

	template <Vector V>
	constexpr auto cross(const V& lhs, const V& rhs)
		requires (VectorTraits<V>::dimension == 3)
	{
		using Traits = VectorTraits<V>;
		V res;
		Traits::setElem(res, 0, Traits::getElem(lhs, 1) * Traits::getElem(rhs, 2) - Traits::getElem(lhs, 2) * Traits::getElem(rhs, 1));
		Traits::setElem(res, 1, Traits::getElem(lhs, 2) * Traits::getElem(rhs, 0) - Traits::getElem(lhs, 0) * Traits::getElem(rhs, 2));
		Traits::setElem(res, 2, Traits::getElem(lhs, 0) * Traits::getElem(rhs, 1) - Traits::getElem(lhs, 1) * Traits::getElem(rhs, 0));
		return res;
	}

	template <Vector V>
	constexpr auto normalize(V& v) {
		auto len = length(v);
		for (auto index = 0; index < VectorTraits<V>::dimension; ++index) {
			VectorTraits<V>::setElem(v, index, VectorTraits<V>::getElem(v, index) / len);
		}
	}

	template <Vector VecA, Vector VecB>
	constexpr auto& operator+=(VecA& a, const VecB& b)
		requires (VectorTraits<VecA>::dimension == VectorTraits<VecB>::dimension)
	{
		for (auto index = 0; index < VectorTraits<VecA>::dimension; ++index) {
			VectorTraits<VecA>::setElem(a, index, VectorTraits<VecA>::getElem(a, index) + VectorTraits<VecB>::getElem(b, index));
		}
		return a;
	}

	template <Matrix MatA, Matrix MatB>
		requires (MatrixTraits<MatA>::dimension.x == MatrixTraits<MatB>::dimension.y)
	constexpr auto operator*(const MatA& a, const MatB& b) {
		using MultiplyResult = decltype(getElem(a, 0, 0)* getElem(b, 0, 0));
		using AdditionResult = decltype(std::declval<MultiplyResult>() + std::declval<MultiplyResult>());
		constexpr auto bWidth = MatrixTraits<MatB>::dimension.x;
		constexpr auto aHeight = MatrixTraits<MatA>::dimension.y;
		constexpr auto depth = MatrixTraits<MatA>::dimension.x;
		auto result = Mat<AdditionResult, bWidth, aHeight>{};
		for (int index = 0; index < bWidth * aHeight; ++index) {
			int cellX = index % bWidth;
			int cellY = index / bWidth;
			auto sum = AdditionResult(0);
			for (int i = 0; i < depth; ++i) {
				sum += getElem(a, i, cellY) * getElem(b, cellX, i);
			}
			setElem(result, cellX, cellY, sum);
		}
		return result;
	}

	template <Matrix M, Vector V>
		requires (MatrixTraits<M>::dimension.x == VectorTraits<V>::dimension)
	constexpr auto operator*(const M& a, const V& b) {
		return toVector(a * toMatrixColumn(b));
	}
}

// Examples of how to make custom builders, in case SimpleBuilders aren't enough

struct VecBuilder2 {
	using PossibleTypes = std::tuple<VecMat::Vec<float, 2>, VecMat::Vec<float, 3>, VecMat::Vec<float, 4>>;

	template <typename Continuation, typename Pool>
	void operator()(Continuation continuation, Pool& pool, lua_State* ls, int idx) const {

		// Read the lua datum and build a c++ object of an appropriate type, or pass std::nullopt in case of failure

		if (lua_type(ls, idx) != LUA_TTABLE) {
			continuation(std::nullopt);
		}

		constexpr int capacity = 4;
		float elms[capacity];
		auto elemCount = LuaStrap::readArrayUpTo<float>(ls, idx, capacity, elms);

		switch (elemCount) {
			break; case 2: continuation(pool.build<VecMat::Vec<float, 2>>(elms[0], elms[1]));
			break; case 3: continuation(pool.build<VecMat::Vec<float, 3>>(elms[0], elms[1], elms[2]));
			break; case 4: continuation(pool.build<VecMat::Vec<float, 4>>(elms[0], elms[1], elms[2], elms[3]));
			break; default: continuation(std::nullopt);
		}
	}
};

struct MatBuilder {
	using PossibleTypes = std::tuple<
		VecMat::Mat<float, 2, 2>, VecMat::Mat<float, 2, 3>, VecMat::Mat<float, 2, 4>,
		VecMat::Mat<float, 3, 2>, VecMat::Mat<float, 3, 3>, VecMat::Mat<float, 3, 4>,
		VecMat::Mat<float, 4, 2>, VecMat::Mat<float, 4, 3>, VecMat::Mat<float, 4, 4>
	>;
	constexpr static bool ambiguous = true;

	template <typename Continuation, typename Pool>
	void operator()(Continuation continuation, Pool& pool, lua_State* ls, int idx) const {
		if (lua_type(ls, idx) != LUA_TTABLE) {
			continuation(std::nullopt);
		}

		constexpr int capacity = 16;
		float elms[capacity];
		auto elemCount = LuaStrap::readArrayUpTo<float>(ls, idx, capacity, &elms[0]);

		auto makeMat = [&]<typename M>() {
			using std::begin;
			auto* mat = pool.build<M>();
			std::copy(begin(elms), begin(elms) + elemCount, begin(mat->elems));
			continuation(mat);
		};

		// For elem counts which are ambiguous, go with the alternative that leads somewhere
		// (won't result in a wrong parameter set, such as a 3*2 mat multiplying another 3*2 mat)

		switch (elemCount) {
			break; case 4:
				makeMat.operator()<VecMat::Mat<float, 2, 2>>();
			break; case 6:
				if constexpr (LuaStrap::leadsAnywhere<Continuation, VecMat::Mat<float, 3, 2>>())
					makeMat.operator()<VecMat::Mat<float, 3, 2>>();
				else
					makeMat.operator()<VecMat::Mat<float, 2, 3>>();
			break; case 8:
				if constexpr (LuaStrap::leadsAnywhere<Continuation, VecMat::Mat<float, 4, 2>>())
					makeMat.operator()<VecMat::Mat<float, 4, 2>>();
				else
					makeMat.operator()<VecMat::Mat<float, 2, 4>>();
			break; case 9:
				makeMat.operator()<VecMat::Mat<float, 3, 3>>();
			break; case 12:
				if constexpr (LuaStrap::leadsAnywhere<Continuation, VecMat::Mat<float, 4, 3>>())
					makeMat.operator()<VecMat::Mat<float, 4, 3>>();
				else
					makeMat.operator()<VecMat::Mat<float, 3, 4>>();
			break; case 16:
				makeMat.operator()<VecMat::Mat<float, 4, 4>>();
			break; default:
				continuation(std::nullopt);
		}
	}
};

// The test

void doVectorMatrixTest(lua_State* ls) {
	namespace lst = LuaStrap;

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

	lst::pushBulkFunc<vecB, vecB>					// < we specify the function's args - 'dot' takes two vectors
		(ls, lstrapFuncWrapper(VecMat::dot));	// < since 'dot' is a template, not a true function, we must wrap it
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

	luaL_dostring(ls, R"delim(

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

	)delim");
}
