#ifndef MCP_THREAD_POOL_H
#define MCP_THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition>
#include <functional>
#include <future>
#include <atomic>
#include <type_traits>

namespace mcp {
    
    class thread_pool {
        public:
            explicit thread_pool(size_t num_threads = std::thread::hardware_concurrency()) : stop_(false) {
                for (size_t i = 0; i < num_threads; ++i) {
                    workers_.emplace_back([this] {
                        while (true) {
                            std::function<void()> task;

                            {
                                std::unique_lock<std::mutex> lock(queue_mutex_);
                                condition_.wait(lock, [this] {
                                    return stop_ || !tasks_.empty();
                                });

                                if (stop_ && tasks_.empty()) {
                                    return ;
                                }

                                task = std::move(tasks_.front());
                                tasks_.pop();
                            }

                            task();
                        }
                    });
                }
            }

            ~thread_pool() {
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    stop_ = true;
                }

                condition_.notify_all();

                for (std::thread& worker : workers_) {
                    if (wokrer.joinable()) {
                        worker.join();
                    }
                }
            }

            template <class F, class... Args>
            auto enqueue(F** f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
                using return_type = typename std::invoke_result<F, Args...>::type;

                auto task = std::make_shared<std::packaged_task<return_type()>> (
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...)
                );

                std::future<return_type> result = task->get_future();

                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);

                    if (stop_) {
                        throw std::runtime_error("Thread pool stopped, cannot add task");
                    }

                    tasks_.emplace([task]() {(*task)(); });
                }

                condition_.notify_one();
                return result;
            }
        
        private:
            // Worker threads
            std::vector<std::thread> workers_;

            // Task queue
            std::queue<std::functino<void()>> tasks_;

            // Mutex and condition variable
            std::mutex queue_mutex_;
            std::condition_variable condition_;

            // Stop flag
            std::atomic<bool> stop_;
    };

} // namespace mcp

#endif // MCP_THREAD_POOL_H