#pragma once

#include "../interfaces/AlgorithmInterface.hpp"
#include "../Commons.hpp"

#include <tuple>
#include <vector>
#include <string>

class ReduceHistogram : public AlgorithmInterface<std::tuple<histogram, histogram>, histogram> {
public:
	ReduceHistogram() = default;

	ReduceHistogram(ReduceHistogram& other) = default;
	ReduceHistogram(ReduceHistogram&& other) = default;

	ReduceHistogram& operator=(const ReduceHistogram& other) = default;
	ReduceHistogram& operator=(ReduceHistogram&& other) = default;

	virtual ~ReduceHistogram() = default;

	histogram Compute(std::tuple<histogram, histogram>&& value) const override {
		histogram value_1 = std::move(std::get<0>(value));
		histogram value_2 = std::move(std::get<1>(value));

		for (auto i = 0; i < value_1.size(); i++) {
			value_1[i] += value_2[i];
		}

		return std::move(value_1);
	}

	std::string Name() const override {
		return std::string("reduce_histogram");
	}
};

class ReduceHistogramKeyed : public AlgorithmInterface<std::tuple<histogram_keyed, histogram_keyed>, histogram_keyed> {
public:
	ReduceHistogramKeyed() = default;

	ReduceHistogramKeyed(ReduceHistogramKeyed& other) = default;
	ReduceHistogramKeyed(ReduceHistogramKeyed&& other) = default;

	ReduceHistogramKeyed& operator=(const ReduceHistogramKeyed& other) = default;
	ReduceHistogramKeyed& operator=(ReduceHistogramKeyed&& other) = default;

	virtual ~ReduceHistogramKeyed() = default;

	histogram_keyed	Compute(std::tuple<histogram_keyed, histogram_keyed>&& value) const override {
		histogram_keyed& value_1 = std::get<0>(value);
		histogram_keyed& value_2 = std::get<1>(value);

		assert(value_1.size() == value_2.size() && "The reduction vectors don't have the same size");

		for (auto j = 0; j < REPEATS_REDUCE; j++) {
			for (auto i = 0; i < value_1.size(); i++) {
				size_t val_1 = std::get<0>(value_1[i]);
				int key_1 = std::get<1>(value_1[i]);

				size_t val_2 = std::get<0>(value_2[i]);
				int key_2 = std::get<1>(value_2[i]);

				assert(key_1 == key_2 && "The keys are not in the same order");

				value_1[i] = std::make_tuple(val_1 + val_2, key_1);
			}
		}

		return std::move(value_1);
	}

	std::string Name() const override {
		return std::string("reduce_histogram_keyed");
	}
};

class ReduceHistogramMap : public AlgorithmInterface<std::tuple<std::map<int, size_t>, std::map<int, size_t>>, std::map<int, size_t>> {
public:
	ReduceHistogramMap() = default;

	ReduceHistogramMap(const ReduceHistogramMap& other) = default;
	ReduceHistogramMap(ReduceHistogramMap&& other) = default;

	ReduceHistogramMap& operator=(const ReduceHistogramMap& other) = default;
	ReduceHistogramMap& operator=(ReduceHistogramMap&& other) = default;

	virtual ~ReduceHistogramMap() = default;

	std::map<int, size_t>	Compute(std::tuple<std::map<int, size_t>, std::map<int, size_t>>&& value) const override {
		std::map<int, size_t>& value_1 = std::get<0>(value);
		std::map<int, size_t>& value_2 = std::get<1>(value);

		for (auto [key, value] : value_2) {
			value_1[key] += value;
		}

		return std::move(value_1);
	}

	std::string Name() const override {
		return std::string("reduce_histogram_map");
	}
};

