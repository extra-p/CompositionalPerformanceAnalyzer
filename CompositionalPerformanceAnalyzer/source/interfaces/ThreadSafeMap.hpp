#pragma once

#include <tuple>
#include <map>
#include <mutex>

template<typename Key, typename T>
class ThreadSafeMap {
	std::map<Key, T> inner_mapping{};
	std::mutex inner_mutex{};

public:
	ThreadSafeMap() = default;

	ThreadSafeMap(ThreadSafeMap& other) = delete;
	ThreadSafeMap(ThreadSafeMap&& other) = delete;

	ThreadSafeMap& operator=(const ThreadSafeMap& other) = delete;
	ThreadSafeMap& operator=(ThreadSafeMap&& other) = delete;

	virtual ~ThreadSafeMap() = default;

	T& get(const Key& key) {
		std::lock_guard<std::mutex>lock(inner_mutex);
		return inner_mapping[key];
	}

	void set(Key key, T value) {
		std::lock_guard<std::mutex>lock(inner_mutex);
		inner_mapping[key] = value;
	}

	void reset() {
		std::lock_guard<std::mutex>lock(inner_mutex);
		inner_mapping.clear();
	}

	std::tuple<Key*, int> getKeyList() {
		std::lock_guard<std::mutex> lock(inner_mutex);

		const auto length = inner_mapping.size();

		auto keys = new Key[length];
		auto ctr = 0;

		for (auto const& entry : inner_mapping) {
			keys[ctr] = entry.first;
			ctr++;
		}

		return std::make_tuple(keys, ctr);
	}
};
