#include "Globals.hpp"

#include "algorithms/Increaser.hpp"
#include "algorithms/Nopper.hpp"
#include "algorithms/QuickSorter.hpp"

#include "interfaces/AlgorithmWrapper.hpp"

#include "pattern/Pipeline.hpp"
#include "pattern/TaskPool.hpp"

#include <iostream>
#include <thread>
#include <vector>

#include <mpi.h>

int main(int argument_count, char** arguments) {
	MPI_Init(&argument_count, &arguments);

	std::cout << "Working with a hardware concurrency of: " << std::thread::hardware_concurrency() << std::endl;

	auto qs = std::make_shared<QuickSorter<int>>();
	auto qs_w = AlgorithmWrapper<std::vector<int>, std::vector<int>>::create(qs);

	auto inc = std::make_shared<Increaser<int>>();
	auto inc_w = AlgorithmWrapper<std::vector<int>, std::vector<int>>::create(inc);

	auto nop = std::make_shared<Nopper<std::vector<int>>>();
	auto nop_w = AlgorithmWrapper<std::vector<int>, std::vector<int>>::create(nop);

	auto tp = TaskPool<std::vector<int>, std::vector<int>>::create(qs_w, 4);

	auto pipe_1 = Pipeline< std::vector<int>, std::vector<int>, std::vector<int>>::create(inc_w, tp);
	auto pipe_2 = Pipeline< std::vector<int>, std::vector<int>, std::vector<int>>::create(pipe_1, nop_w);

	std::vector<std::future<std::vector<int>>> inputs{};
	for (auto i = 0; i < 10; i++) {
		std::promise< std::vector<int>> promise{};

		std::vector<int> vec{};
		for (auto i = 0; i < 1000; i++) {
			vec.push_back((((i + 100) * 13) + 53) % 83);
		}

		promise.set_value(std::move(vec));
		inputs.emplace_back(promise.get_future());
	}

	std::vector<std::future<std::vector<int>>> outputs{};
	std::vector<std::vector<int>> unpacked_outputs{};

	pipe_2->Init();

	for (auto& input : inputs) {
		outputs.emplace_back(pipe_2->Compute(std::move(input)));
	}

	for (auto& output : outputs) {
		unpacked_outputs.emplace_back(output.get());
	}

	pipe_2->Dispose();

	std::cout << "Finished" << std::endl;

	MPI_Finalize();

	return 0;
}
