#pragma once

#include <future>
#include <memory>
#include <tuple>
#include <vector>

namespace {
	int ARRAY_SIZE = 1024 * 1024;
	int ARRAY_SIZE_REDUCTION = 1024;
	int ARRAY_SIZE_MAP_REDUCE = 1024 * 8;

	int SCALE_EXTRA_P = 1;
	int EXTRA_P_REPEATS = 1;
	int MR_SZE = 1;

	int NUMBER_INPUTS = 64;

	int INNER_SIZE = 16;
	int OUTER_SIZE = NUMBER_INPUTS / INNER_SIZE;

	int SHUFFLE_KEYS = 7;

	bool ASSERT_SORTED_ARRAY = true;

	bool VERBOSE_TEST_OUTPUT = true;

	int SLICES = 8;

	int MATRIX_MAX_SIZE = 256;
	int MATRIX_NUMBER = 1024 * 64;

	constexpr const int MIN_RANDOM_INT = 0;
	constexpr const int MAX_RANDOM_INT = 65535;

	constexpr const int MAX_RANDOM_KEY_INT = 63;

	constexpr const int REPEATS_REDUCE = 1;
}

enum class RGB : int {
	T = 3,
	R = 2,
	G = 1,
	B = 0
};


typedef std::vector<size_t> histogram;
typedef std::vector < std::tuple<size_t, int>> histogram_keyed;

template<typename T1, typename T2>
class PatternInterface;

template<typename T1, typename T2>
using PatIntPtr = std::shared_ptr<PatternInterface<T1, T2>>;


template<typename T1, typename T2>
class AlgorithmInterface;

template<typename T1, typename T2>
using AlgoIntPtr = std::shared_ptr<AlgorithmInterface<T1, T2>>;


template<typename T1, typename T2>
class AlgorithmWrapper;

template<typename T1, typename T2>
using AlgoWrapPtr = std::shared_ptr<AlgorithmWrapper<T1, T2>>;

template<typename T>
using FutVec = std::vector<std::future<T>>;
