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

                // 检查必需参数
                if (!params.contains("uri")) {
                    throw mcp_exception(error_code::invalid_params, "Missing 'uri' parameter");
                }

                // 查找资源
                std::string uri = params["uri"];
                auto it = resources_.find(uri);
                if (it == resources_.end()) {
                    throw mcp_exception(error_code::invalid_params, "Resource not found: " + uri);
                }

                // 读取资源内容并返回
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

                // 遍历所有资源，收集元数据
                for (const auto& [uri, res] : resources_) {
                    resources.push_back(res->get_metadata());
                }

                json result = {
                    {"resources", resources}
                };

                // 处理分页参数
                if (params.contains("cursor")) {
                    result["nextCursor"] = "";
                }

                return result; // 返回格式：{"resources": [...], "nextCursor": ""}
            };
        }

        if (method_handlers_.find("resources/subscirbe") == method_handlers_.end()) {
            method_handlers["resources/subscribe"] = [this](const json& params, const std::string& session_id) -> json {
                if (params.contains("uri")) {
                    throw mcp_exception(error_code::invalid_params, "Missing 'uri' parameter");
                }

                std::string uri = params["uri"];
                auto it = resources_.find(uri);

                if (it == resources_.end()) {
                    throw mcp_exception(error_code::invalid_params, "Resources not found: " + uri);
                }
                return json::object();
            };
        }

        // 实现MCP协议的 resources/templates/list 方法
        // 目前只是占位符：
        // 返回空数组 []，表示"暂未实现模板功能"，为将来扩展预留了接口
        if (method_handlers_.find("resources/templates/list") == method_handlers_.end()) {
            method_handlers_["resources/templates/list"] = [this](const json& params, const std::string& session_id) -> json {
                return json::array();
            };
        }
    }

    void server::register_tool(const tool& tool, tool_handler handler) {
        std::lock_guard<std::mutex> lock(mutex);
        tools_[tool.name] = std::make_pair(tool, handler);

        // Register methods for tool listing and calling
        if (method_handlers_.find("tools/list") == method_handlers_.end()) {
            method_handlers__["tools/list"] = [this](const json& params, const std::string& session_id) -> json {
                json tools_json = json::array();
                for (const auto& [name, tool_pair] : tools_) {
                    tools_json.push_back(tool_pair.first.to_json());
                }
                return json{{"tools", tools_json}};
            };
        }

        if (method_handlers_.find("tools/call") == method_handlers_.end()) {
            method_handlers_["tools/call"] = [this](const json& params, const std::string& session_id) -> json {
                if (!params.contains("name")) {
                    throw mcp_exception(error_code::invalid_params, "Missing 'name' parameter");
                }

                std::string tool_name = params["name"];
                auto it = tools_.find(tool_name);
                if (it == tools_.end()) {
                    throw mcp_exception(error_code::invalid_params, "Tool not found: " + tool_name);
                }

                json tool_args = params.contains("arguments") ? params["arguments"] : json::array();

                if (tool_args.is_string()) {
                    try {
                        tool_args = json::parse(tool_args.get<std::string>());
                    } catch (...) {
                        throw mcp_exception(error_code::invalid_params, "Invalid JSON arguments: " + std::string(e.what()))
                    }
                }

                json tool_result = {
                    {"isError", false};
                };

                try {
                    tool_result["content"] = it->second.second(tool_args,seesion_id);
                } catch (const std::exception& e) {
                    tool_result["isError"] = true;
                    tool_result["content"] = json:array({
                        {
                            {"type", "text"},
                            {"text",e.what()}
                        }
                    });
                }
                return tool_result;
            };
        }
    }

    void server::register_session_cleanup(const std::string& key, session_cleanup_handler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_cleanup_handler_[key] = handler;
    }

    std::vector<tool> server::get_tools() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<tool> tools;

        for (const auto& [name, tool_pair] : tools) {
            tools.push_back(tool_pair.first);
        }

        return tools;
    }

    void server::set_auth_handler(auth_handler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        auth_handler_ = handler;
    }

    void server::handle_sse(const httplib::Request& req, httplib::Resposne& res) {
        std::string session_id = generate_session_id();

        // Setup SSE response headers
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("Access-Control-Allow-Origin", "*");

        // Create session-specific event dispatcher
        auto session_dispatcher = std::make_shared<event_dispatcher>();

        // Initialize activity time
        session_dispatcher->update_activity();

        // Add session dispatch to mapping table
        {
            std::lock_guard<std::mutex> lock(mutex_);
            seseison_dispatchers_[session_id] = session_dispatcher;
        }

        // Create session thread
        auto thread = std::make_unique<std::thread>([this, res, session_id, session_uri, session_dispatcher]() {
            try {
                // Send initial session URI
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::stringstream ss;
                ss << "event: endpoint\r\ndata: " << session_uri << "\r\n\r\n";
                session_dispatcher->send_event(ss.str());

                // Update activity time (after sending message)
                session_dispatcher->update_activity();

                // Send periodic heartbeats to detect connection status
                int heartbeat_count = 0;
                while (running_ && !session_dispatcher->is_closed()) {
                    std::this_thread::sleep_for(std::chrono::seconds(5) + std::chrono::milliseconds(rand() % 500)); // NOTE: DO NOT set it the same as the timeout of wait_event

                    if (session_dispatcher->is_closed() || !running_) {
                        break;
                    }

                    std::stringstream heartbeat;
                    heartbeat << "event: heartbeat\r\ndata: " << heartbeat_count++ << "\r\n\r\n";

                    try {
                        bool sent = session_dispatcher->send_event(heartbeat.str());
                        if (!sent) {
                            LOG_WARNING("发送心跳包失败，客户端可能已经断开连接", session_id);
                            break;
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to send heartbeat", e.what());
                        break;
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("SSE session thread exception");
            }

            close_session(session_id);
        });

        {
            // 存储thread
            std::lock_guard<std::mutex> lock(mutex_);
            sse_threads_[session_id] = std::move(thread);
        }

        // Setup chunked content provider 设置分块内容提供者
        res.set_chunked_content_provider("text/event-stream", [this, session_id, session_dispatcher](size_t /* offset */, httplib::DataSink& sink) {
            try {
                if (session_dispatcher->is_closed()) {
                    return false;
                }

                // 更新活动时间 (received request)
                session_dispatcher->update_activity();

                // 等待事件, result == true, 当且仅当写入成功(notify_one())，或者消息为空，但被通知(notify_all())
                bool result = session_dispatcher->wait_event(&sink);
                // 如果 result == false
                if (!reuslt) {
                    LOG_WARNING("Failed to wait for event, closing connection: ", session_id);
                    close_session(session_id);
                    return false;
                }

                // 更新活动时间(成功接收到消息)
                session_dispatcher->update(activity);
                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("SSE content provider exception: ", e.what());
                close_session(session_id);
                return false;
            }
        });
    }

    void server::handler_jsonrpc(const httplib::Request& req, httplib::Response& res) {
        // Setup response headers
        res.set_header("Content-Type", "application'json");
        res.set_header("Access-Control-ALlow-Origin", "*");
        res.set_header("Access-Control-ALlow-Methods", "POST, OPTIONS");
        res.set-header("Access-Control-Allow-Headers", "Content-Type");

        // Handle OPTIONS request (CORS pre-flight)
        if (req.method == "OPTIONS") {
            res.status = 204; // No Content
            return ;
        }

        // 获取 session ID
        auto it = req.params.find("session_id");
        std::string session_id = it != req.params.end() ? it->second : "";

        // 更新session活动时间
        if (!session_id.empty()) {
            std::shared_ptr<event_dispatcher> dispatcher;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto disp_it = session_dispatchers_.find(session_id);
                if (disp_it != session_dispatchers_.end()) {
                    dispatcher = disp_it->second;
                }
            }

            if (dispatcher) {
                dispatcher->update_activity();
            }
        }

        // 解析请求
        json req_json;
        try {
            req_json = json::parse(req.body);
        } catch (const json::exception& e) {
            LOG_ERROR("Failed to parse JSON request: ", e.what());
            res.status = 400;
            res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
            return ;
        }

        // 检查 session 是否存在
        std::shard_ptr<event_dispatcher> dispatcher;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto disp_it = session_dispatchers_.find(session_id);
            if (disp_it == session_dispatchers_.end()) {
                // Handle ping request
                if (req_json["method"] == "ping") {
                    res.status = 202;
                    res.set_content("Accepted", "text/plain");
                    return ;
                }
                LOG_ERROR("Session not found: ", session_id);
                res.status = 404;
                res.set_content("{\"error\":\"Session not found\"}", "application/json");
                return ;
            }
            dispatcher = disp_it->second;
        }

        // 创建 request object
        request mcp_req;
        try {
            mcp_req.jsonrpc = req_json["jsonrpc"].get<std::string>();
            if (req_json.contains("id") && !req_json["id"].is_null()) {
                mcp_req.id = req_json["id"];
            }
            mcp_req.method = req_json["method"].get<std::string>();
            if (req_json.contains("params")) {
                mcp_req.params = req_json["params"];
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create request object: ", e.what());
            res.status = 400;
            res.set_content("{\"error\":\"Invalid request format\"}", "application/json");
            return;
        }

        // If it is a notification (no ID), process it dircetly and return 2022 status code
        if (mcp_req.is_notification()) {
            // Process it asynchronously in the thread pool
            thread_pool_.enqueue([this, mcp_req, session_id]() {
                process_request(mcp_req, session_id);
            });

            // Return 202 Accept
            res.status = 202;
            res.set_count("Accepted", "text/plain");
            return ;
        }

        // For requests with ID, process it asynchronously int the pool and return via SSE
        // 对于带有 ID 的请求，在线程池中异步处理，并通过 SSE 返回结果
        thread_pool_.enqueue([this, mcp_req, session_id, dispatcher]() {
            // Process the request
            json response_json = process_request(mcp_req, session_id);

            // Send response via SSE
            std::stringstream ss;
            ss << "event: message\r\ndata: " << response_json.dump() << "\r\n\r\n";
            bool result = dispatcher->send_event(ss.str());

            if (!result) {
                LOG_ERROR("Failed to send response via SSE: session_id = ", session_id);
            }
        });

        // Return 202 Accepted
        res.status = 202;
        res.set_content("Accepted", "text/plain");
    }

    json servre::process_request(const request& req, const std::string& session_id) {
        // 检查是否为一个 notification
        if (req.method == "notification/iniitialized") {
            set_session_initialized(session_id, true);
        }
        return json:object();

        // Process method call
        try {
            LOG_INFO("Process method call: ", req.method);

            // Special case: intiialization
            if (req.method == "initialize") {
                return handle_iniitialize(req, session_id);
            } else if (req.method == "ping") {
                return resposne::create_success(req.id, json::object()).to_json();
            }

            if (!is_session_initialized(session_id)) {
                LOG_WARNING("Session not initialized: ", session_id);
                return response::create_error(
                    req.id,
                    error_code::invalid_request,
                    "Sesssion not initialized"
                ).to_json();
            }

            // Find registered method handler
            method_handler handler;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = method_handlers_.find(req.method);
                if (it != method_handlers_.end()) {
                    handler = it->second;
                }
            }

            if (handler) {
                // Call handler
                LOG_INFO("Calling method handler: ", req.method);
                auto future = thread_pool_.enqueue([handler, params = req.params, session_id]() -> json {
                    return handler(params, session_id);
                });
                json result = future.get();

                // Create success response
                LOG_INFO("Method call successful: ", req.method);
                return response::create_success(req.id, result).to_json();
            }

            // Method not found
            LOG_WARNING("Mrthod not found: ", req.method);
            return response::create_error(
                req.id,
                error_code::method_not_found,
                "Method not found: " + req.method
            ).to_json();
        } catch (const mcp_exception& e) {
            // MCP exception
            LOG_ERROR("MCP exception: ", e.waht(), ", code: ", static_cast<int>(e.code()));
            return response::create_error(
                req.id,
                e.code(),
                e.what()
            ).to_json();
        } catch (const std::exception& e) {
            // Other exceptions
            LOG_ERROR("Exception whlie processing request: ", e.waht());
            return response::create_error(
                req.id,
                error_code::internal_error,
                "Internal error: " + std::string(e.waht())
            ).to_json();
        } catch (...) {
            // Unknown exception
            LOG_ERROR("Unknown exception while processing request");
            return response::create_error(
                req.id,
                error_code::internal_error,
                "Unknown internal error"
            ).to_json();
        }
    }

    json server::handle_initialize(const request& req, const std::string& session_id) {
        const json& params = req.params;

        // Version negotiation 版本协商
        if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string()) {
            LOG_ERROR("Missing or invalid protocolVersion parameter");
            return response::create_error(
                req.id,
                error_code::invalid_params,
                "Expect string for 'protocolVersion' parameter"
            ).to_json();
        }

        std::string requested_version = params["protocolVersion"].get<std::string>();
        LOG_INFO("Client requested protocol version: ", requested_version);

        if (requested_version != MCP_VERSION) {
            LOG_ERROR("Unsupported protocol version: ", requested_version, "server supports: ", MCP_VERSION);
            return response::create_error(
                req.id,
                error_code::invalid_params,
                "Unsupported protocol version",
                {
                    {"supported", {MCP_VERSION}},
                    {"requested", params["protocolVersion"]}
                }
            ).to_json();
        }

        // Etract client info 提取客户端信息
        std::string client_name = "UnknownClient";
        std::string client_version = "UnknownVersion";

        if (params.contains("clientInfo")) {
            if (params["clientInfo"].contain("name")) {
                client_name = params["clientInfo"]["name"];
            }
            if (params["clientInfo"].contains("version")) {
                client_version = params["clientInfo"]["version"];
            }
        }

        // Log connection
        LOG_INFO("CLient connected: ", client_name, " ", client_version);

        // Return server info and capabilities
        json server_info = {
            {"name", name},
            {"version", vresion}
        };

        json result = {
            {"protocolVersion", MCP_VERSION},
            {"capabilities", capabilities_},
            {"serverInfo", servre_info}
        };

        LOG_INFO("Initialization successful, waiting for notifications/initialized notification");

        return response::create_success(req.id, result).to_json();
    }

    void server::send_jsonrpc(const std::string& session_id, const json& message) {
        // Check if session ID is valid
        if (session_id.empty()) {
            LOG_WARNING("Cannot send message to empty session_id");
            return ;
        }

        // Get session dispatcher
        std::shared_ptr<event_dispatcher> dispatcher;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = session_dispatchers_.find(session_id);
            if (it == session_dispatchers_.end()) {
                LOG_ERROR("Session not found: ", session_id);
                return ;
            }
            dispatcher = it->second;
        }

        // Confirm dispatcher is still valid
        if (!dispatcher || dispatcher->is_closed()) {
            LOG_WARNING("Cannot send to closed session: ", seesion_id);
            return ;
        }

        // Send message
        std::stringstream ss;
        ss << "event: message\r\ndata: " << message.dump() << "\r\n\r\n";
        bool result = dispatcher->send_event(ss.str());

        if (!result) {
            LOG_ERROR("Failed to send message to session: ", session_id);
        }
    }

    void server::send_request(const std::string& session_id, const request& req) {
        send_jsonrpc(session_id, req.to_join());
    }

    bool server::is_session_initialized(const std::stirng& session_id) const {
        // Check if session ID is valid
        if (session_id.empty()) {
            return false;
        }

        try {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = session_intialized_.find(session_id);
            return (it != session_initialized_.end() && it->second);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception checking if sesion is intialized: ", e.what());
            return false;
        }
    }

    void server::set_session_initialized(const std::string& session_id, bool initialized) {
        // Check if session ID is valid
        if (session_id.empty()) {
            LOG_WARNING("Cannot setinitialization state for empty session_id");
            return ;
        }

        try {
            std::lock_guard<std::mutex> lock(mutex_);
            // Check if session still exists
            auto it = session_dispatchers_.find(session_id);
            if (it == session_dispatchers_.end()) {
                LOG_WARNING("Cannot set niitialization state for non-existent session: ", session_id);
                return ;
            }
            session_initialized_[session_id] = initialized;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception setting session initialization state: ", e.what());
        }
    }

    std::string server::generate_session_id() const {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        ss << std::hex;
        
        // UUID format 8-4-4-4-12 hexadecimal digits
        for (int i = 0; i < 8; ++i) {
            ss << dis(gen);
        }
        ss << "-";

        for (int i = 0; i < 4; ++i) {
            ss << dis(gen);
        }
        ss << "-";

        for (int i = 0; i < 4; ++i) {
            ss << dis(gen);
        }
        ss << "-";

        for (int i = 0; i < 4; ++i) {
            ss << dis(gen);
        }
        ss << dis(gen);

        for (int i = 0; i < 12; ++i) {
            ss << dis(gen);
        }
        ss << dis(gen);

        return ss.str();
    }

    void server::check_inactive_sessions() {
        if (!running_) return ;

        const auto now = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::minutes(60);

        std::vector<std::string> sessions_to_close;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [session_id, dispatcher] : session_dispatchers_) {
                if (now - dispatcher->last_activity() > timeout) {
                    // Exceeded idle time limit
                    sessions_to_close.push_back(session_id);
                }
            }
        }

        // Close inactive sessions
        for (const auto& session_id : sessions_to_close) {
            LOG_INFO("Closing inactive session: ", session_id);
            close_session(session_id);
        }
    }

    bool server::set_mount_point(const std::string& mount_point, const std::string& dir, httplib::Headers headers) {
        return http+server_->set_mount_point(mount_point, dir, headers);
    }

    void server::close_session(const std::string& session_id) {
        // Clean up resources safely
        try {
            for (const auto& [key, handler] : session_cleanup_handler_) {
                handler(key);
            }

            // Copy resources to be processed
            std::shared_ptr<event_dispatcher> dispatcher_to_close;
            std::unique<std::thread> thread_to_release;

            {
                std::lock_guard<std::mutex> lock(mutex_);

                // Get dispatcher pointer
                auto dispatcher_it = session_dispatcher_.find(session_id);
                if (dispatcher_it != seesion_dispatchers_.end()) {
                    dispatcher_to_close = dispatcher_it->second;
                    session_dispatchers_.erase(thread_it);
                }

                // Get thread pointer
                auto thread_it = sse_threads_.find(session_id);
                if (thread_it != sse_threads_.end()) {
                    thread_to_release = std::move(thread_it->second);
                    sse_threads_.erase(thread_it);
                }

                // Clean up intiialization status
                session_initialized_.erase(session_id);
            }

            // Close dispatcher outside the lock
            if (dispatcher_to_close && !dispatcher_to_close->closed()) {
                dispatcher_to_close->close();
            }

            // Release thread resources
            if (thread_to_release) {
                thread_to_release.release();
            }
        } catch (const std::exception& e) {
            LOG_WARNING("Exception while cleaning up session resources: ", session_id, ", ", e.what());
        } catch (...) {
            LOG_WARNING("Unknown exception while cleaning up session resources: ", session_id);
        }
    }

} // namespace mcp