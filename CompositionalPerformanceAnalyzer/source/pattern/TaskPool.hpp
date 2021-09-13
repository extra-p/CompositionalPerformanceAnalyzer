#pragma once

#include "../Commons.hpp"
#include "../interfaces/PatternInterface.hpp"
#include "../interfaces/ThreadSafeQueue.hpp"
#include "../interfaces/Executor.hpp"

#include <cassert>
#include <future>
#include <thread>
#include <vector>

template <typename T_input, typename  T_output>
class TaskPool : public PatternInterface<T_input, T_output> {
	std::vector<std::thread> threads{};

	Executor<T_input, T_output> executor{};

	TSQueue<std::tuple<std::future<T_input>, std::promise<T_output>>> inner_queue{};

	void PerformTask() {
		while (!this->dying) {
			std::tuple<std::future<T_input>, std::promise<T_output>> data{};
			bool success = this->inner_queue.try_pop(data);

			if (!success) {
				if (this->dying) {
					break;
				}

				std::this_thread::yield();
				continue;
			}

			auto future = std::move(std::get<0>(data));
			auto promise = std::move(std::get<1>(data));

			executor.Compute(std::move(future), std::move(promise));
		}
	}

	TaskPool(PatIntPtr<T_input, T_output>& task, size_t thread_count)
		: threads(thread_count), executor(task, thread_count) { }

protected:
	void InternallyCompute(std::future<T_input> future, std::promise<T_output> promise) override {
		inner_queue.push(std::make_tuple(std::move(future), std::move(promise)));
	}

public:
	static PatIntPtr<T_input, T_output> create(PatIntPtr<T_input, T_output> task, size_t thread_count) {
		assert(thread_count > 0);

		auto pool = new TaskPool(task, thread_count);
		auto s_ptr = std::shared_ptr<PatternInterface<T_input, T_output>>(pool);
		return s_ptr;
	}

	TaskPool(const TaskPool& other) = delete;
	TaskPool(TaskPool&& other) = delete;

	TaskPool& operator=(const TaskPool& other) = delete;
	TaskPool& operator=(TaskPool&& other) = delete;

	size_t ThreadCount() const noexcept override {
		return threads.size() * executor.ThreadCount();
	}

	std::string Name() const override {
		return std::string("taskpool(") + std::to_string(threads.size()) + std::string(",") + executor.Name() + std::string(")");
	}

	PatIntPtr<T_input, T_output> create_copy() override {
		this->assertNoInit();

		auto copied_version = create(executor.GetTask(), threads.size());
		return copied_version;
	}

	void Init() override {
		if (!this->initialized) {
			this->dying = false;

			executor.Init();

			for (unsigned i = 0; i < threads.size(); i++) {
				threads[i] = std::thread(&TaskPool::PerformTask, this);
			}

			this->initialized = true;
		}
	}

	void Dispose() override {
		if (this->initialized) {
			this->dying = true;

			for (std::thread& thread : threads) {
				if (thread.joinable()) {
					thread.join();
				}
			}

			executor.Dispose();

			this->initialized = false;
		}
	}

	~TaskPool() {
		TaskPool<T_input, T_output>::Dispose();
	}
};
