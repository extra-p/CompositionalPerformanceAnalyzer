#pragma once

#include "../Commons.hpp"
#include "../interfaces/PatternInterface.hpp"
#include "../interfaces/ThreadSafeQueue.hpp"
#include "../interfaces/Executor.hpp"

#include <future>
#include <thread>

template<typename T_input, typename T_intermediate, typename T_output>
class Pipeline : public PatternInterface<T_input, T_output> {
	Executor<T_input, T_intermediate> executor1{};
	Executor<T_intermediate, T_output> executor2{};

	std::thread stage1_thread{};
	std::thread stage2_thread{};

	TSQueue<std::tuple<std::future<T_input>, std::promise<T_intermediate>>> first_queue{};
	TSQueue<std::tuple<std::future<T_intermediate>, std::promise<T_output>>> second_queue{};

	void PerformFirstStage() {
		while (!this->dying) {
			std::tuple<std::future<T_input>, std::promise<T_intermediate>> data{};
			bool success = this->first_queue.try_pop(data);

			if (!success) {
				if (this->dying) {
					break;
				}
				std::this_thread::yield();
				continue;
			}

			auto future = std::move(std::get<0>(data));
			auto promise = std::move(std::get<1>(data));

			executor1.Compute(std::move(future), std::move(promise));
		}
	}

	void PerformSecondStage() {
		while (!this->dying) {
			std::tuple<std::future<T_intermediate>, std::promise<T_output>> data{};
			bool success = this->second_queue.try_pop(data);

			if (!success) {
				if (this->dying) {
					break;
				}
				std::this_thread::yield();
				continue;
			}

			auto future = std::move(std::get<0>(data));
			auto promise = std::move(std::get<1>(data));

			executor2.Compute(std::move(future), std::move(promise));
		}
	}

	Pipeline(PatIntPtr<T_input, T_intermediate> interface1, PatIntPtr<T_intermediate, T_output> interface2)
		: executor1(interface1), executor2(interface2) { }

protected:
	void InternallyCompute(std::future<T_input> future, std::promise<T_output> promise) override {
		std::promise<T_intermediate> intermediate_promise{};
		std::future<T_intermediate> intermediate_future = intermediate_promise.get_future();

		first_queue.push(std::make_tuple(std::move(future), std::move(intermediate_promise)));
		second_queue.push(std::make_tuple(std::move(intermediate_future), std::move(promise)));
	}

public:
	static PatIntPtr<T_input, T_output> create
		(PatIntPtr<T_input, T_intermediate> interface1, PatIntPtr<T_intermediate, T_output> interface2) {
		auto pipe = new Pipeline(interface1, interface2);
		auto s_ptr = std::shared_ptr<PatternInterface<T_input, T_output>>(pipe);
		return s_ptr;
	}

	Pipeline(Pipeline& other) = delete;
	Pipeline(Pipeline&& other) = delete;

	Pipeline& operator=(const Pipeline& other) = delete;
	Pipeline& operator=(Pipeline&& other) = delete;

	PatIntPtr<T_input, T_output> create_copy() override {
		this->assertNoInit();

		auto copied_version = create(executor1.GetTask(), executor2.GetTask());
		return copied_version;
	}

	size_t ThreadCount() const noexcept override {
		return executor1.ThreadCount() + executor2.ThreadCount() + 2;
	}

	std::string Name() const override {
		return std::string("pipeline(") + executor1.Name() + std::string(",") + executor2.Name() + std::string(")");
	}

	void Init() override {
		if (!this->initialized) {
			this->dying = false;

			executor1.Init();
			executor2.Init();

			stage1_thread = std::thread(&Pipeline::PerformFirstStage, this);
			stage2_thread = std::thread(&Pipeline::PerformSecondStage, this);

			this->initialized = true;
		}
	}

	void Dispose() override {
		if (this->initialized) {
			this->dying = true;

			stage1_thread.join();
			stage2_thread.join();

			executor1.Dispose();
			executor2.Dispose();

			this->initialized = false;
		}
	}

	~Pipeline() {
		Pipeline<T_input, T_intermediate, T_output>::Dispose();
	}
};
