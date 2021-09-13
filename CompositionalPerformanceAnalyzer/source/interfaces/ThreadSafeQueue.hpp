#pragma once


#include "../Commons.hpp"

#include "concurrentqueue-master/concurrentqueue.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>


namespace QueueImplementations {
	template<typename T>
	class QueueInterface {
	public:
		QueueInterface() = default;

		QueueInterface(QueueInterface& other) = delete;
		QueueInterface(QueueInterface&& other) = delete;

		QueueInterface& operator=(const QueueInterface& other) = delete;
		QueueInterface& operator=(QueueInterface&& other) = delete;

		virtual ~QueueInterface() = default;

		virtual void push(T new_val) = 0;

		virtual bool try_pop(T& value) = 0;
	};

	template<typename T>
	class RefCountingCameronQueue :public QueueInterface<T> {
		moodycamel::ConcurrentQueue<T> queue;

	public:
		void push(T new_val) override {
			queue.enqueue(std::move(new_val));
		}

		bool try_pop(T& value) override { 
			return queue.try_dequeue(value);
		}
	};
}

template<typename T>
using TSQueue = QueueImplementations::RefCountingCameronQueue<T>;

