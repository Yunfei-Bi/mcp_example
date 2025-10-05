#include "mcp_server.h"

namespace mcp {
    server::server(const std::string& host, int port, const std::string& name, const std::string& version, const std::string& sse_endpoint, const std::string& msg_endpoint)
        : host_(host), port_(port), name_(name), version_(version), sse_endpoint_(sse_endpoint), msg_endpoint_(msg_endpoint) {
            http_server_ = std::make_unique<httplib::Server>();
        }
    
    server::~server() {
        stop();
    }

    void server::stop() {
        if (!running_) {
            return ;
        }

        LOG_INFO("Stopping MCP server on ", host_, ":", port_);
        running_ = false;

        // CLose maintenance thread
        if (maintenance_thread_ && maintenance_thread_->joinable()) {
            try {
                maintenance_thread_->join();
            } catch (...) {
                maintenance_thread_->detach();
            }
        }

        // Copy all dispatchers and threads to avoid holding the lock for too long
        std::vector<std::shared_ptr<event_dispatcher>> dispatchers_to_close;
        std::vector<std::unique_ptr<std::thread>> threads_to_join;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Copy all dispatchers
            dispatchers_to_close.reserve(session_dispatchers_.size());
            for (const auto& [_, dispatcher] : session_dispatchers_) {
                dispatchers_to_close.push_back(dispatcher);
            }

            // Copy all threads
            threads_to_join.reserve(sse_threads_.size());
            for (auto& [_, thread] : sse_threads_) {
                if (thread && thread->joinable()) {
                    threads_to_join.push_back(std::move(thread));
                }
            }

            // Clear the maps
            session_dispatchers_.clear();
            sse_threads_.clear();
            session_initialized_.clear();
        }

        // CLose all sessions
        for (const auto& [session_id, _] : session_dispatchers_) {
            close_session(session_id);
        }

        // Give threads some time to handle close events
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Wait for threads to finish outside the lock (with timeout limit)
        const auto timeout_point = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        for (auto& thread : threads_to_join) {
            if (!thread || !thread->joinable()) {
                continue;
            }

            if (std::chrono::steady_clock::now() >= timeout_point) {
                // If timeout reached, detach remaining threads
                LOG_WARNING("Thread join timeout reached, detaching remaining threads");
                thread->detach();
                continue;
            }

            bool joined = false;
            try {
                std::promise<void> thread_done;
                auto future = thread_done.get_future();

                std::thread join_helper([&thread, &thread_done]() {
                    try {
                        thread->join();
                        thread_done.set_value(); // 把关联的 future 标记为就绪，让 future.wait_for(...) 变为 ready
                    } catch (...) {
                        try {
                            // 若在 join 过程中出错，用 set_exception(...) 把异常关联到 future；
                            // 等待方随后 future.get() 会抛出该异常
                            thread_done.set_exception(std::current_exception());
                        } catch (...) {}
                    }
                });

                // Wait for join to complete or timeout
                if (future.wait_for(std::chrono::illiseconds(100)) == std::future_status::ready) {
                    future.get();
                    joined = true;
                }

                // Process join_helper thread
                if (join_helper.joinable()) {
                    if (joined) {
                        join_helper.join();
                    } else {
                        join_heper.detach();
                    }
                }
            } catch (...) {
                joined = false;
            }

            // IF join fails, then detach
            if (!joined) {
                try {
                    thread->detach();
                } catch (...) {
                    // Ignore exceptions
                }
            }
        }

        if (server_thread_ && server_thread_->joinable()) {
            http_server_->stop();
            try {
                server_thread_->join();
            } catch (...) {
                server_thread_->detach();
            }
        } else {
            http_server_->stop();
        }

        LOG_INFO("MCP server stopped");
    }

    bool server::is_running() const {
        return running_;
    }

    void server::set_server_info(const std::string& name, const std::string& version) {
        std::lock_guard<std::mutex> lock(mutex_);
        name_ = name;
        version_ = version;
    }

    void server::set_capabilities(const json& capabilities) {
        std::lock_guard<std::mutex> lock(mutex_);
        capabilities_ = capabilities;
    }

    void server::register_method(const std::string& method, method_handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        method_handlers_[method] = handler;
    }

    void server::register_notification(const std::string& method, notification_handler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        notification_handlers_[method] = handler;
    }

    void server::register_resource(const std::string& path, std::shared_ptr<resource> resource) {
        std::lock_guard<std::mutex> lock(mutex_);
        resources_[path] = resource;

        // Register methods for resource access
        if (method_handlers_.find("resources/read") == method_handlers_.end()) {
            method_handlers_["resources/read"] = [this](cosnt json& params, cosnt std::string& session_id) -> json {
                if (!params.contains("uri")) {
                    throw mcp_exception(error_code::invalid_params, "Missing 'uri' parameter");
                }

                std::string uri = params["uri"];
                auto it = resources_.find(uri);
                if (it == resources_.end()) {
                    throw mcp_exception(error_code::invalid_params, "Resource not found: " + uri);
                }

                json contents = json::array();
                contents.push_back(it->second->read());

                return json{
                    {"contents", contents}
                };
            };
        }

        if (method_handlers_.find("resources/list") == method_handlers_.end()) {
            method_handlers_["resources/list"] = [this](const json& params, const std::string& session_id) -> json {
                json resources = json::array();

                for (const auto& [uri, res] : resources_) {
                    resources.push_back(res->get_metadata());
                }

                json_result = {
                    {"resources", resources}
                };

                if (params.contains("cursor")) {
                    result["nextCursor"] = "";
                }

                return result;
            };
        }
    }
} // namespace mcp