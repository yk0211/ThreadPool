#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <cstdlib>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool
{
public:
	explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency())
	{
		if (thread_count < 1)
		{
			thread_count = 1;
		}

		workers_.reserve(thread_count);

		for (size_t i = 0; i < thread_count; i++)
		{
			workers_.emplace_back([this]
			{
				for (;;)
				{
					std::packaged_task<void()> task;

					{
						std::unique_lock<std::mutex> lock(mtx_);
						cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

						if (stop_ && tasks_.empty())
						{
							return;
						}

						task = std::move(tasks_.front());
						tasks_.pop();
					}

					task();
				}
			});
		}
	} 

	~ThreadPool()
	{
		{
			std::unique_lock<std::mutex> lock(mtx_);
			stop_ = true;
		}

		cv_.notify_all();

		for (auto& worker : workers_)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}
	}
    
    ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	template<class Fun,class... Args >
	auto Enqueue(Fun&& fun, Args&&... args) -> std::future<std::invoke_result_t<Fun, Args...>>
	{
		using return_type = std::invoke_result_t<Fun, Args...>;

		std::packaged_task<return_type()> task(
			std::bind(std::forward<Fun>(fun),std::forward<Args>(args)...));

		std::future<return_type> future = task.get_future();

		{
			std::unique_lock<std::mutex> lock(mtx_);
			if (stop_)
			{
				throw std::runtime_error("enqueue a task into thread pool but the pool is stopped");
			}

			tasks_.emplace(std::move(task));
		}

		cv_.notify_one();

		return future;
	}
private:
	std::vector<std::thread> workers_;
	std::queue<std::packaged_task<void()>> tasks_;
	std::mutex mtx_;
	std::condition_variable cv_;
	bool stop_ = false;
};

#endif
