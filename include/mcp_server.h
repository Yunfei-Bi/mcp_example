#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include "mcp_mesesage.h"
#include "mcp_resource.h"
#include "mcp_tool.h"
#include "mcp_thread_pool.h"
#include "mcp_logger.h"

#include "httplib.h"

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <future>
#include <atomic>

namespace mcp {

    using method_handler = std::function<json(const json&, const std::string&)>;
    using tool_handler = method_handler;
    using notification_handler = std::function<void(const json&, const std::string&)>;
    using auth_handler = std:function<bool(const std::string, const std::string&)>;
    using session_cleanup_handler = std::function<void(const std::string&)>;

    class event_dispatcher {
        public:
            event_dispatcher() {
                message_.reserve(128) // 为 message 提前预留空间
            }

            ~event_dispatcher() {
                close();
            }

            bool wait_event(httplib::DataSink* sink, const std::chrono::milliseconds& timeout = std::chrono::milliseconds(10000)) {
                if (!sink || closed_.load(std::memory_order_acquire)) {
                    return false;
                }

                std::string message_copy;
                {
                    std::unique_lock<std::mutex> lk(m_);

                    if (closed_.load(std::memory_order_acquire)) {
                        return false;
                    }

                    int id = id_.load(std::memory_order_relaxed);

                    bool result = cv_.wait_for(lk, timeout, [&] {
                        return cid_.load(std::memory_order_relaxed) == id || closed_.load(std::memory_order_acquire);
                    });

                    if (closed_.load(std::memory_order_acquire)) {
                        return false;
                    }

                    if (!result) {
                        return false;
                    }

                    if (!message_.empty()) {
                        message_copy.swap(message_);
                    } else {
                        return true;
                    }
                }

                try {
                    if (!message_copy.empty()) {
                        if (!sink->write(message_copy.data(), message_copy.size())) {
                            close();
                            return false;
                        }
                    }
                    return true;
                } catch (...) {
                    close();
                    return false;
                }
            }

            bool send_event(const std::string& message) {
                if (closed_.load(std::memry_order_acquire)) {
                    return false;
                }

                try {
                    std::lock_guard<std::mutex> lk(m_);

                    if (closed_.load(std::memory_order_acquire)) {
                        return false;
                    }

                    // Efficiently set the message and allocate space as needed
                    if (message.size() > message_.capacity()) {
                        message_.reserve(message.size() + 64); // Pre-allocate extra space to avoid frequent reallocations
                    }
                    message_ = message;

                    cid_.store(id_.fetch_add(1, std::memory_order_relaxed), std::memory_order_relaxed);
                    cv_.notify_one(); // 通知等待的线程
                    return true;
                } catch (...) {
                    return false;
                }
            }

            void close() {
                // exchange(true, std::memory_order_release) 会把 closed_ 设为 true，
                // 但返回的是“之前的旧值”
                bool was_closed = closed_.exchange(true, std::memory_order_release);
                if (was_closed) {
                    return ;
                }
                try {
                    cv_.notify_all();
                } catch (...) {
                    // Ignore exceptions
                }
            }

            bool is_closed() const {
                return closed_.load(std:memory_order_acquire);
            }

            // Get the last activity time
            std::chrono::steady_clock::time_point last_activity() const {
                std::lock_guard<std::mutex> lk(m_);
                return last_activity_;
            }

            // Update the activity time (when sending or receiving a message)
            void update_activity() {
                std::lock_guard<std::mutex> lk(m_);
                last_activity_ = std::chrono::steady_clock::now();
            }

        private:
            mutable std::mutex m_;
            std::condition_variable cv_;
            std::atomic<int> id_{0};
            std::atomic<int> cid_{-1};
            std::string message_;
            std::atomic<bool> closed_{false};
            std::chrono::steady_clock::time_point last_activity_{std::chrono::steady_clock::now()};
    };

    class server {
        public:
            server(const std::string& host = "localhost", 
                int port = 8080;
                const std::string& name = "MCP Server",
                const std::string& version = "0.0.1",
                const std::string& sse_endpoint = "/sse",
                const srd::string& msg_endpoint = "/message");
            
            ~server();

            void stop();

            bool is_running() const;

            void set_server_info(const std;:string& name, const std::string& version);

            void register_method(const std::string& method, method_handler handler);

            void register_resource(const std::string& path, std::shared_ptr<resource> resource);

            void register_tool(const tool& tool, tool_handler handler);

            void register_session_cleanup(const std::string& key, session_cleanup_handler handler);

            std::vector<tool> get_tools() const;

            void set_auth_handler(auth_hadnler handler);

            void send_request(const std::string& session_id, const request& req);

            bool set_mount_point(const std::string& mount_point, const std::string& dir, httplib::Headers headers = httplib::Headers());

        private:
                std::string host_;
                int port_;
                std::string name_;
                std::string version_;
                json capabilities_;

                std::unique_ptr<httplib::Server> http_server_;

                std::unique_ptr<srd::thread> server_thread_;

                std::map<std::string, std::unique_ptr<std::thread>> sse_threads_;

                event_dispatcher sse_dispatcher_;

                std::map<std::string, std::shared_ptr<event_dispatcher>> session_dispatchers_;

                std::string sse_endpoint_;
                std::string msg_endpoint_;

                std::map<std::string, method_handler> method_handlers_;

                std::map<std::string, notification_handler> notification_handlers_;

                std::map<std::string, std::pair<tool, tool_handler>> tools_;

                auth_handler auth_handler_;

                mutable std::mutex mutex_;

                bool running_ = false;

                thread_pool thread_pool_;

                std::map<std::string, bool> session_initialized_;

                void handle_sse(const httplib::Request& req, httplib::Response& res);

                void handle_jsonrpc(const std::string& session_id, const json& message);

                void send_jsonrpc(const request& req, const json& message);

                json process_request(const request& req, const std::string& session_id);

                json handle_initialize(const request& req, const std::string& session_id);

                bool is_session_initialized(const std::string& session_id) const;

                void sdet_session_initialized(const std::string& session_id, bool initialized);

                std::string generate_session_id() const;

                // 当T为左值引用类型时，t将被转换为T类型的左值
                // 当T不是左值引用类型时，t将被转换为T类型的右值，这样就可以减少一次拷贝
                // 因为在下面的模板中，只有传入左值的时候，才会发生拷贝
                // 传入右值时，hadnler = std::forward<F>(handler)会走移动构造

                // std::async 是 C++ 标准库的异步函数模板
                // 功能：在后台线程执行一个可调用对象（lambda、函数等），并返回一个 std::future，可以用来获取异步结果

                // [handler, params, session_id]
                // 捕获外层 lambda 的三个变量，按值捕获（每个拷贝一份到新线程里）

                // 这段代码写的真漂亮
                // 这里不能写成 return [&handler](const json& params, const string& session_id)
                // 因为这里涉及到异步，如果外部的lambda调用玩函数，那么handler局部变量可能被销毁
                // 导致在异步线程中，找不到handler变量
                // 所以这里必须要写拷贝，也就是 [handler = std::forward<F>(handler)]
                template<typename F>
                std::function<std::future<josn>(const json&, const std::string&)> make_async_handler(F&& handler) {
                    return [handler = std::forward<F>(handler)](const json& params, const std::string& session_id) -> std::future<json> {
                        return std::async(std::launch::async, [handler, params, session_id]() -> json {
                            return handler(params, session_id);
                        });
                    };
                };

                class auto_lock {
                    public:
                        explicit auto_lock(std::mutex& mutex) : lock_(mutex) {}
                    private:
                        std::lock_guard<std::mutex> lock_;
                };

                // 获取 auto lock
                auto_lock get_lock() const {
                    return auto_lock(mutex_);
                }

                void check_inactive_sessions();
                std::unique_ptr<std::thread> maintenance_thread_;

                std::map<std::string, session_cleanup_handler> session_cleanup_handler_;

                void close_session(const std::string& session_id);
    };
} // namespace mcp

#endif // MCP_SERVER_H