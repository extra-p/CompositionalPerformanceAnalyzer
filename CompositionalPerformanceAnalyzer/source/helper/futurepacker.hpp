#pragma once

#include "../Commons.hpp"

#include <future>
#include <vector>

template<typename T>
FutVec<T> future_packer(std::vector<T>&& vector) {
	FutVec<T> result_vector;

	for (size_t i = 0; i < vector.size(); i++) {
		std::promise<T> promise;
		promise.set_value(std::move(vector[i]));

		std::future<T> future = promise.get_future();

		result_vector.emplace_back(std::move(future));
	}

	return result_vector;
}

template<typename T>
std::vector<T> future_getter(FutVec<T>&& vector) {
	std::vector<T> result_vector(vector.size());

	for (size_t i = 0; i < vector.size(); i++) {
		result_vector[i] = std::move(vector[i].get());
	}

	return result_vector;
}
