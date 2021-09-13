#pragma once

#include "../Commons.hpp"
#include "../interfaces/PatternInterface.hpp"
#include "../interfaces/Executor.hpp"

#include "../helper/mpi_helper.hpp"

#include <mpi.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>
#include <cassert>
#include <memory>
#include <map>
#include <mutex>
#include <shared_mutex>


constexpr const int pre_size = 768;

template<typename T_key, typename T_value>
class ThreadSafeKeyedCollection {
	std::vector<std::unique_ptr<std::mutex>> mutexes{};
	std::vector<std::vector<T_value>> values{};

	std::shared_mutex containment_mutex{};

public:
	ThreadSafeKeyedCollection() :mutexes(pre_size), values(pre_size) {
		for (T_key key = 0; key < pre_size; key++) {
			mutexes[key] = std::move(std::make_unique<std::mutex>());
		}
	}

	void preAddKeys(std::vector<T_key>& vector) {
		std::unique_lock<std::shared_mutex> lock(containment_mutex);

		for (auto key : vector) {
			mutexes[key] = std::move(std::make_unique<std::mutex>());
			values[key] = std::vector<T_value>{};
		}
	}

	void add(const T_key& key, T_value& value) {
		std::unique_ptr<std::mutex>& mutex = mutexes[key];
		mutex->lock();

		std::vector<T_value>& vec = values[key];
		vec.emplace_back(value);

		mutex->unlock();
	}

	void add(const T_key& key, std::vector<T_value>& vals) {
		std::unique_ptr<std::mutex>& mutex = mutexes[key];
		mutex->lock();

		std::vector<T_value>& vec = values[key];
		vec.insert(vec.end(), vals.begin(), vals.end());

		mutex->unlock();
	}

	void get(std::vector<std::vector<T_value>>& result) {
		result = std::move(values);
		values = std::vector<std::vector<T_value>>{};
		mutexes = std::vector<std::unique_ptr<std::mutex>>{};
	}

	void get(std::map<T_key, std::vector<T_value>>& result) {
		for (T_key key = 0; key < pre_size; key++) {
			result[key] = std::move(values[key]);
		}
		values = std::vector<std::vector<T_value>>(768);
		mutexes = std::vector<std::unique_ptr<std::mutex>>(768);

		for (T_key key = 0; key < pre_size; key++) {
			mutexes[key] = std::move(std::make_unique<std::mutex>());
		}
	}
};


template<typename T_input, typename T_output, typename T_key, typename T_map_result>
class MapReduceGlobalH : public PatternInterface<FutVec<T_input>, std::map<T_key, T_output>> {

	typedef std::map<T_key, T_map_result> map_result;
	typedef std::map<T_key, T_output> end_result;

	std::vector<std::thread> threads{};

	Executor<T_input, map_result> mapper{};
	Executor<std::vector<T_output>, T_output> reducer{};

	AlgoIntPtr<T_key, int> distributer{};

	TSQueue<std::tuple<std::future<T_input>, std::promise<map_result>>> map_queue{};
	TSQueue<std::tuple<std::future<map_result>, ThreadSafeKeyedCollection<T_key, T_output>*, std::promise<void>>> shuffle_queue{};
	TSQueue<std::tuple<std::future<std::vector<T_output>>, std::promise<T_output>>> reduce_queue{};

	size_t mpi_nodes{};

	bool PerformMapFunction() {
		std::tuple<std::future<T_input>, std::promise<map_result>> tuple{};

		auto success = map_queue.try_pop(tuple);

		if (!success) {
			return false;
		}

		auto future = std::move(std::get<0>(tuple));
		auto promise = std::move(std::get<1>(tuple));

		mapper.Compute(std::move(future), std::move(promise));

		return true;
	}

	bool PerformShuffleFunction() {
		std::tuple<std::future<map_result>, ThreadSafeKeyedCollection<T_key, T_output>*, std::promise<void>> tuple_queue{};

		auto success = shuffle_queue.try_pop(tuple_queue);

		if (!success) {
			return false;
		}

		auto future = std::move(std::get<0>(tuple_queue));
		auto tskc = std::move(std::get<1>(tuple_queue));
		auto promise = std::move(std::get<2>(tuple_queue));

		auto result = future.get();

		for (auto [key, value] : result) {
			tskc->add(key, value);
		}

		promise.set_value();

		return true;
	}

	bool PerformReduceFunction() {
		std::tuple<std::future<std::vector<T_output>>, std::promise<T_output>> tuple_queue{};

		auto success = reduce_queue.try_pop(tuple_queue);

		if (!success) {
			return false;
		}

		auto future = std::move(std::get<0>(tuple_queue));
		auto promise = std::move(std::get<1>(tuple_queue));

		reducer.Compute(std::move(future), std::move(promise));

		return true;
	}

	void Perform() {
		while (!this->dying) {
			bool success_map = PerformMapFunction();

			if (success_map) {
				continue;
			}

			bool success_shuffle = PerformShuffleFunction();

			if (success_shuffle) {
				continue;
			}

			bool success_reduce = PerformReduceFunction();

			if (success_reduce) {
				continue;
			}

			std::this_thread::yield();
		}
	}


	void MergeMaps(std::map<T_key, std::vector<T_output>>& lhs, std::map<T_key, std::vector<T_output>>& rhs) {
		for (auto [key, vec] : rhs) {
			std::vector<T_output>& lhs_vec = lhs[key];
			lhs_vec.insert(lhs_vec.end(), vec.begin(), vec.end());
		}
	}

	void SendToNode(int index, std::map<T_key, std::vector<T_output>>& to_send) {
		std::vector<T_key> keys{};
		std::vector<int> sizes{};

		for (auto [key, value] : to_send) {
			keys.emplace_back(key);
			sizes.emplace_back(to_send[key].size());
		}

		size_t size = to_send.size();
		mpi_send_size(index, size);

		mpi_send_global(keys, index);
		mpi_send_global(sizes, index);

		for (auto i = 0; i < size; i++) {
			std::vector<T_output>& vec = to_send[keys[i]];
			mpi_send_global(vec, index);
		}
	}

	void ReceiveFromNode(int index, std::map<T_key, std::vector<T_output>>& to_receive) {
		size_t count = mpi_receive_size(index);

		auto keys = new T_key[count];
		auto sizes = new int[count];

		mpi_receive_global(keys, count, index);
		mpi_receive_global(sizes, count, index);

		for (auto j = 0; j < count; j++) {
			auto vec = std::vector<T_output>(sizes[j]);
			mpi_receive_global(vec.data(), sizes[j], index);

			to_receive[keys[j]] = std::move(vec);
		}

		delete[] keys;
		delete[] sizes;
	}

	void CommunicateWithNode(int src_idx, int dst_idx, std::map<T_key, std::vector<T_output>>& to_send, std::map<T_key, std::vector<T_output>>& to_receive) {
		if (src_idx < dst_idx) {
			SendToNode(dst_idx, to_send);
			ReceiveFromNode(dst_idx, to_receive);
		}
		else {
			ReceiveFromNode(dst_idx, to_receive);
			SendToNode(dst_idx, to_send);
		}
	}

	void ShuffleNodes(std::map<T_key, std::vector<T_output>>& values) {
		if (mpi_nodes < 2) {
			return;
		}

		std::vector<std::map<T_key, std::vector<T_output>>> individual_maps(mpi_nodes);

		for (auto it = values.begin(); it != values.end(); it++) {
			auto key = it->first;
			auto value = std::move(it->second);

			int node_to_receive_this_key = distributer->Compute(std::move(key));

			assert(node_to_receive_this_key < mpi_nodes && "The node is not available");

			std::map<T_key, std::vector<T_output>>& map = individual_maps[node_to_receive_this_key];
			map[it->first] = std::move(value);
		}

		int mpi_rank = -1;
		mpi_get_global_rank(&mpi_rank);

		std::map<T_key, std::vector<T_output>>& mine = individual_maps[mpi_rank];

		for (auto i = 0; i < mpi_nodes; i++) {
			if (i == mpi_rank) {
				continue;
			}

			std::map<T_key, std::vector<T_output>> tmp_dst_map;
			std::map<T_key, std::vector<T_output>>& src_map = individual_maps[i];

			CommunicateWithNode(mpi_rank, i, src_map, tmp_dst_map);

			MergeMaps(mine, tmp_dst_map);
		}

		values = std::move(mine);
	}

	MapReduceGlobalH(PatIntPtr<T_input, map_result> mapper_task, PatIntPtr<std::vector<T_output>, T_output> reducer_task, size_t threads, size_t mpi_nodes,
		AlgoIntPtr<T_key, int> distributer)
		: mapper(mapper_task), reducer(reducer_task), threads(threads), mpi_nodes(mpi_nodes), distributer(distributer) {
	}

	void ReduceAtEnd(end_result& mr) {
		size_t size = mr.size();

		if (mpi_nodes < 2) {
			return;
		}

		int mpi_rank = -1;

		mpi_get_global_rank(&mpi_rank);

		if (mpi_rank > 0) {
			std::vector<T_key> keys;
			std::vector<T_output> values;

			for (auto [key, value] : mr) {
				keys.emplace_back(key);
				values.emplace_back(value);
			}

			mpi_send_global(&size, 1, 0);

			mpi_send_global(keys, 0);
			mpi_send_global(values, 0);

			mr.clear();

			return;
		}

		std::map<T_key, std::vector<T_output>> intermediate_map{};

		for (auto [key, val] : mr) {
			intermediate_map[key] = { val };
		}

		for (auto i = 1; i < mpi_nodes; i++) {
			size_t count;

			mpi_receive_global(&count, 1, i);

			auto keys = new T_key[count];
			auto vals = new T_output[count];

			mpi_receive_global(keys, count, i);
			mpi_receive_global(vals, count, i);

			for (auto j = 0; j < count; j++) {
				auto& tmp_vec = intermediate_map[keys[j]];
				tmp_vec.emplace_back(vals[j]);
			}
		}

		mr.clear();

		for (auto [key, vals] : intermediate_map) {
			auto res = reducer.Compute(std::move(vals));
			mr[key] = res;
		}
	}


protected:
	void InternallyCompute(std::future<FutVec<T_input>> future, std::promise<end_result> promise) override {
		std::vector<std::future<T_input>> inputs = future.get();
		std::vector<std::future<void>> shuffle_await_vector{};

		ThreadSafeKeyedCollection<T_key, T_output> tskc{};

		for (std::future<T_input>& input : inputs) {
			std::promise<void> prom_void;
			shuffle_await_vector.emplace_back(prom_void.get_future());

			std::promise<map_result> prom_res;
			auto tup_shuffle = std::make_tuple(prom_res.get_future(), &tskc, std::move(prom_void));
			auto tup_map = std::make_tuple(std::move(input), std::move(prom_res));

			map_queue.push(std::move(tup_map));
			shuffle_queue.push(std::move(tup_shuffle));
		}

		for (auto& fut : shuffle_await_vector) {
			fut.get();
		}

		std::map<T_key, std::vector<T_output>> shuffled_values{};
		tskc.get(shuffled_values);

		ShuffleNodes(shuffled_values);

		std::map<T_key, std::future<T_output>> holding_map{};

		for (auto [key, vals] : shuffled_values) {
			std::promise<std::vector<T_output>> prom;

			prom.set_value(std::move(vals));

			std::promise<T_output> res_prom;
			std::future<T_output> res_fut = res_prom.get_future();

			auto tup_red = std::make_tuple(prom.get_future(), std::move(res_prom));
			reduce_queue.push(std::move(tup_red));

			holding_map[key] = std::move(res_fut);
		}

		end_result end_result{};

		for (auto& [key, vals] : holding_map) {
			end_result[key] = holding_map[key].get();
		}

		ReduceAtEnd(end_result);

		promise.set_value(std::move(end_result));
	}

public:
	static PatIntPtr<FutVec<T_input>, end_result> create(
		PatIntPtr<T_input, map_result> mapper_task, PatIntPtr<std::vector<T_output>, T_output> reducer_task, size_t threads, size_t mpi_nodes,
		AlgoIntPtr<T_key, int> distributer) {
		assert(threads > 0 && mpi_nodes > 0);

		auto mr = new MapReduceGlobalH(mapper_task, reducer_task, threads, mpi_nodes, distributer);
		auto s_ptr = PatIntPtr<FutVec<T_input>, end_result>(mr);
		return s_ptr;
	}

	MapReduceGlobalH(const MapReduceGlobalH& other) = delete;
	MapReduceGlobalH(MapReduceGlobalH&& other) = delete;

	MapReduceGlobalH& operator=(const MapReduceGlobalH& other) = delete;
	MapReduceGlobalH& operator=(MapReduceGlobalH&& other) = delete;

	PatIntPtr<FutVec<T_input>, end_result> create_copy() override {
		this->assertNoInit();

		auto copy = create(mapper.GetTask(), reducer.GetTask(), threads.size(), mpi_nodes, distributer);
		return copy;
	}

	void Init() override {
		if (!this->initialized) {
			this->dying = false;

			mapper.Init();
			reducer.Init();

			for (unsigned i = 0; i < threads.size(); i++) {
				threads[i] = std::thread(&MapReduceGlobalH::Perform, this);
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

			mapper.Dispose();
			reducer.Dispose();

			this->initialized = false;
		}
	}

	size_t ThreadCount() const noexcept override {
		return threads.size() + (mapper.ThreadCount() + reducer.ThreadCount());;
	}

	std::string Name() const override {
		return std::string("MapReduceGlobalH(") + mapper.Name() + "," + reducer.Name() + "," + std::to_string(threads.size()) + "," + std::to_string(mpi_nodes) + ")";
	}

	~MapReduceGlobalH() {
		MapReduceGlobalH::Dispose();
	}
};


template<typename T_input, typename T_output, typename T_key, typename T_map_result>
class MapReduceLocalH : public PatternInterface<FutVec<T_input>, std::map<T_key, T_output>> {

	typedef std::map<T_key, T_map_result> map_result;
	typedef std::map<T_key, T_output> end_result;

	std::vector<std::thread> threads{};

	Executor<T_input, map_result> mapper{};
	Executor<std::vector<T_output>, T_output> reducer{};

	TSQueue<std::tuple<std::future<T_input>, std::promise<map_result>>> map_queue{};
	TSQueue<std::tuple<std::future<map_result>, ThreadSafeKeyedCollection<T_key, T_output>*, std::promise<void>>> shuffle_queue{};
	TSQueue<std::tuple<std::future<std::vector<T_output>>, std::promise<T_output>>> reduce_queue{};

	size_t mpi_nodes{};

	bool PerformMapFunction() {
		std::tuple<std::future<T_input>, std::promise<map_result>> tuple;

		auto success = map_queue.try_pop(tuple);

		if (!success) {
			return false;
		}

		auto future = std::move(std::get<0>(tuple));
		auto promise = std::move(std::get<1>(tuple));

		mapper.Compute(std::move(future), std::move(promise));

		return true;
	}

	bool PerformShuffleFunction() {
		std::tuple<std::future<map_result>, ThreadSafeKeyedCollection<T_key, T_output>*, std::promise<void>> tuple_queue{};

		auto success = shuffle_queue.try_pop(tuple_queue);

		if (!success) {
			return false;
		}

		auto future = std::move(std::get<0>(tuple_queue));
		auto tskc = std::move(std::get<1>(tuple_queue));
		auto promise = std::move(std::get<2>(tuple_queue));

		auto result = future.get();

		for (auto [key, value] : result) {
			tskc->add(key, value);
		}

		promise.set_value();

		return true;
	}

	bool PerformReduceFunction() {
		std::tuple<std::future<std::vector<T_output>>, std::promise<T_output>> tuple_queue{};

		auto success = reduce_queue.try_pop(tuple_queue);

		if (!success) {
			return false;
		}

		auto future = std::move(std::get<0>(tuple_queue));
		auto promise = std::move(std::get<1>(tuple_queue));

		reducer.Compute(std::move(future), std::move(promise));

		return true;
	}

	void Perform() {
		while (!this->dying) {
			bool success_map = PerformMapFunction();

			if (success_map) {
				continue;
			}

			bool success_shuffle = PerformShuffleFunction();

			if (success_shuffle) {
				continue;
			}

			bool success_reduce = PerformReduceFunction();

			if (success_reduce) {
				continue;
			}

			std::this_thread::yield();
		}
	}


	void ReduceAtEnd(end_result& mr) {
		size_t size = mr.size();

		if (mpi_nodes < 2) {
			return;
		}

		int mpi_rank = -1;

		mpi_get_global_rank(&mpi_rank);

		if (mpi_rank > 0) {
			std::vector<T_key> keys;
			std::vector<T_output> values;

			for (auto [key, value] : mr) {
				keys.emplace_back(key);
				values.emplace_back(value);
			}

			mpi_send_global(&size, 1, 0);

			mpi_send_global(keys, 0);
			mpi_send_global(values, 0);

			mr.clear();

			return;
		}

		std::map<T_key, std::vector<T_output>> intermediate_map;

		for (auto [key, val] : mr) {
			intermediate_map[key] = { val };
		}

		for (auto i = 1; i < mpi_nodes; i++) {
			size_t count;

			mpi_receive_global(&count, 1, i);

			auto keys = new T_key[count];
			auto vals = new T_output[count];

			mpi_receive_global(keys, count, i);
			mpi_receive_global(vals, count, i);

			for (auto j = 0; j < count; j++) {
				auto& tmp_vec = intermediate_map[keys[j]];
				tmp_vec.emplace_back(vals[j]);
			}
		}

		mr.clear();

		for (auto [key, vals] : intermediate_map) {
			auto res = reducer.Compute(std::move(vals));
			mr[key] = res;
		}
	}


	MapReduceLocalH(PatIntPtr<T_input, map_result> mapper_task, PatIntPtr<std::vector<T_output>, T_output> reducer_task, size_t threads, size_t mpi_nodes)
		: mapper(mapper_task), reducer(reducer_task), threads(threads), mpi_nodes(mpi_nodes) {
	}

protected:
	void InternallyCompute(std::future<FutVec<T_input>> future, std::promise<end_result> promise) override {
		std::vector<std::future<T_input>> inputs = future.get();
		std::vector<std::future<void>> shuffle_await_vector{};

		ThreadSafeKeyedCollection<T_key, T_output> tskc{};

		for (std::future<T_input>& input : inputs) {
			std::promise<void> prom_void;
			shuffle_await_vector.emplace_back(prom_void.get_future());

			std::promise<map_result> prom_res;
			auto tup_shuffle = std::make_tuple(prom_res.get_future(), &tskc, std::move(prom_void));
			auto tup_map = std::make_tuple(std::move(input), std::move(prom_res));

			map_queue.push(std::move(tup_map));
			shuffle_queue.push(std::move(tup_shuffle));
		}

		for (auto& fut : shuffle_await_vector) {
			fut.get();
		}

		std::vector<std::vector<T_output>>  shuffled_values{};
		tskc.get(shuffled_values);

		if (mpi_nodes > 1) {
			mpi_sync_global();
		}

		std::vector<std::tuple<std::future<T_output>, T_key>> reduce_results{};

		for (T_key key = 0; key < pre_size; key++) {
			auto val = shuffled_values[key];
			std::promise<std::vector<T_output>> prom_reduce;
			prom_reduce.set_value(std::move(val));

			std::promise<T_output> prom_result;
			reduce_results.emplace_back(std::make_tuple(prom_result.get_future(), key));

			reduce_queue.push(std::make_tuple(prom_reduce.get_future(), std::move(prom_result)));
		}

		end_result result{};

		for (auto& reduce_result : reduce_results) {
			auto key = std::move(std::get<1>(reduce_result));
			auto fut_output_val = std::move(std::get<0>(reduce_result));
			auto output_val = fut_output_val.get();

			result[key] = output_val;
		}

		ReduceAtEnd(result);

		promise.set_value(std::move(result));
	}

public:
	static PatIntPtr<FutVec<T_input>, end_result> create(
		PatIntPtr<T_input, map_result> mapper_task, PatIntPtr<std::vector<T_output>, T_output> reducer_task, size_t threads, size_t mpi_nodes) {
		assert(threads > 0 && mpi_nodes > 0);

		auto mr = new MapReduceLocalH(mapper_task, reducer_task, threads, mpi_nodes);
		auto s_ptr = PatIntPtr<FutVec<T_input>, end_result>(mr);
		return s_ptr;
	}

	MapReduceLocalH(const MapReduceLocalH& other) = delete;
	MapReduceLocalH(MapReduceLocalH&& other) = delete;

	MapReduceLocalH& operator=(const MapReduceLocalH& other) = delete;
	MapReduceLocalH& operator=(MapReduceLocalH&& other) = delete;

	PatIntPtr<FutVec<T_input>, end_result> create_copy() override {
		this->assertNoInit();

		auto copy = create(mapper.GetTask(), reducer.GetTask(), threads.size(), mpi_nodes);
		return copy;
	}

	void Init() override {
		if (!this->initialized) {
			this->dying = false;

			mapper.Init();
			reducer.Init();

			for (unsigned i = 0; i < threads.size(); i++) {
				threads[i] = std::thread(&MapReduceLocalH::Perform, this);
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

			mapper.Dispose();
			reducer.Dispose();

			this->initialized = false;
		}
	}

	size_t ThreadCount() const noexcept override {
		return threads.size() + (mapper.ThreadCount() + reducer.ThreadCount());;
	}

	std::string Name() const override {
		return std::string("MapReduceLocalH(") + mapper.Name() + "," + reducer.Name() + "," + std::to_string(threads.size()) + "," + std::to_string(mpi_nodes) + ")";
	}

	~MapReduceLocalH() {
		MapReduceLocalH::Dispose();
	}
};


template<typename T_input, typename T_output, typename T_key>
class MapReduceLocalV : public PatternInterface<FutVec<T_input>, std::map<T_key, T_output>> {

	typedef std::map<T_key, T_output> map_result;

	std::vector<std::thread> threads{};

	Executor<T_input, map_result> mapper{};
	Executor<std::tuple<map_result, map_result>, map_result> reducer{};

	TSQueue<std::tuple<std::future<T_input>, std::promise<map_result>>> map_queue{};
	TSQueue<std::tuple<map_result, map_result, std::promise<map_result>>> reduce_queue{};

	size_t mpi_nodes{};

	bool PerformMapFunction() {
		std::tuple<std::future<T_input>, std::promise<map_result>> tuple{};

		auto success = map_queue.try_pop(tuple);

		if (!success) {
			return false;
		}

		auto future = std::move(std::get<0>(tuple));
		auto promise = std::move(std::get<1>(tuple));

		mapper.Compute(std::move(future), std::move(promise));

		return true;
	}

	bool PerformReduceFunction() {
		std::tuple<map_result, map_result, std::promise<map_result>> tuple_queue{};

		auto success = reduce_queue.try_pop(tuple_queue);

		if (!success) {
			return false;
		}

		auto future_1 = std::move(std::get<0>(tuple_queue));
		auto future_2 = std::move(std::get<1>(tuple_queue));
		auto prom = std::move(std::get<2>(tuple_queue));

		auto tup = std::make_tuple(std::move(future_1), std::move(future_2));

		std::promise<std::tuple<map_result, map_result>> p{};
		p.set_value(std::move(tup));

		auto tf = p.get_future();

		reducer.Compute(std::move(tf), std::move(prom));

		return true;
	}

	void Perform() {
		while (!this->dying) {
			bool success_map = PerformMapFunction();

			if (success_map) {
				continue;
			}

			bool success_reduce = PerformReduceFunction();

			if (success_reduce) {
				continue;
			}

			std::this_thread::yield();
		}
	}


	void ReduceAtEnd(map_result& mr) {
		size_t size = mr.size();

		if (mpi_nodes < 2) {
			return;
		}

		int mpi_rank = -1;
		mpi_get_global_rank(&mpi_rank);

		if (mpi_rank > 0) {
			std::vector<T_key> keys{};
			std::vector<T_output> values{};

			for (auto [key, value] : mr) {
				keys.emplace_back(key);
				values.emplace_back(value);
			}

			mpi_send_size(0, size);

			mpi_send_global(keys, 0);
			mpi_send_global(values, 0);

			mr.clear();

			return;
		}

		std::vector<map_result> intermediate_results(mpi_nodes);
		intermediate_results[0] = std::move(mr);

		for (auto i = 1; i < mpi_nodes; i++) {
			size_t count = mpi_receive_size(i);

			auto keys = new T_key[count];
			auto vals = new T_output[count];

			mpi_receive_global(keys, count, i);
			mpi_receive_global(vals, count, i);

			map_result tmp_map{};

			for (auto j = 0; j < count; j++) {
				auto& key = keys[j];
				auto& val = vals[j];
				tmp_map[key] = val;
			}

			intermediate_results[i] = std::move(tmp_map);
		}

		auto int_size = intermediate_results.size();
		auto int_idx = 0;

		for (auto i = 0; i < int_size - 1; i++) {
			auto first_index = int_idx++;
			auto second_index = int_idx++;

			auto& first = intermediate_results[first_index];
			auto& second = intermediate_results[second_index];

			auto res = reducer.Compute(std::make_tuple(std::move(first), std::move(second)));
			intermediate_results.emplace_back(std::move(res));
		}

		mr = std::move(intermediate_results[intermediate_results.size() - 1]);
	}


	MapReduceLocalV(PatIntPtr<T_input, map_result> mapper_task, PatIntPtr<std::tuple<map_result, map_result>, map_result> reducer_task, size_t threads, size_t mpi_nodes)
		: mapper(mapper_task), reducer(reducer_task), threads(threads), mpi_nodes(mpi_nodes) {
	}

protected:
	void InternallyCompute(std::future<FutVec<T_input>> future, std::promise<map_result> promise) override {
		FutVec<T_input> inputs = future.get();

		FutVec<map_result> outputs{};

		for (std::future<T_input>& input : inputs) {
			std::promise<map_result> prom{};
			auto fut = prom.get_future();
			auto tup_map = std::make_tuple(std::move(input), std::move(prom));

			map_queue.push(std::move(tup_map));
			outputs.emplace_back(std::move(fut));
		}

		auto idx = 0;

		for (auto i = 0; i < inputs.size() - 1; i++) {
			auto first_index = idx++;
			auto second_index = idx++;

			auto& first_f = outputs[first_index];
			auto& second_f = outputs[second_index];

			auto first_r = first_f.get();
			auto second_r = second_f.get();

			std::promise<map_result> p{};
			std::future<map_result> fut = p.get_future();

			outputs.emplace_back(std::move(fut));

			auto tup = std::make_tuple(std::move(first_r), std::move(second_r), std::move(p));
			reduce_queue.push(std::move(tup));
		}

		auto& last = outputs[outputs.size() - 1];
		map_result result = last.get();

		ReduceAtEnd(result);

		promise.set_value(std::move(result));
	}

public:
	static PatIntPtr<FutVec<T_input>, map_result> create(
		PatIntPtr<T_input, map_result> mapper_task, PatIntPtr<std::tuple<map_result, map_result>, map_result> reducer_task, size_t threads, size_t mpi_nodes) {
		assert(threads > 0 && mpi_nodes > 0);

		auto mr = new MapReduceLocalV(mapper_task, reducer_task, threads, mpi_nodes);
		auto s_ptr = PatIntPtr<FutVec<T_input>, map_result>(mr);
		return s_ptr;
	}

	MapReduceLocalV(const MapReduceLocalV& other) = delete;
	MapReduceLocalV(MapReduceLocalV&& other) = delete;

	MapReduceLocalV& operator=(const MapReduceLocalV& other) = delete;
	MapReduceLocalV& operator=(MapReduceLocalV&& other) = delete;

	PatIntPtr<FutVec<T_input>, map_result> create_copy() override {
		this->assertNoInit();

		auto copy = create(mapper.GetTask(), reducer.GetTask(), threads.size(), mpi_nodes);
		return copy;
	}

	void Init() override {
		if (!this->initialized) {
			this->dying = false;

			mapper.Init();
			reducer.Init();

			for (unsigned i = 0; i < threads.size(); i++) {
				threads[i] = std::thread(&MapReduceLocalV::Perform, this);
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

			mapper.Dispose();
			reducer.Dispose();

			this->initialized = false;
		}
	}

	size_t ThreadCount() const noexcept override {
		return threads.size() + (mapper.ThreadCount() + reducer.ThreadCount());;
	}

	std::string Name() const override {
		return std::string("MapReduceLocalV(") + mapper.Name() + "," + reducer.Name() + "," + std::to_string(threads.size()) + "," + std::to_string(mpi_nodes) + ")";
	}

	~MapReduceLocalV() {
		MapReduceLocalV::Dispose();
	}
};

