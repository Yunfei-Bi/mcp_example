#ifndef MCP_STDIO_CLIENT_H
#define MCP_STDIO_CLIENT_H

#include "mcp_client.h"
#include "mcp_message.h"
#include "mcp_tool.h"
#include "mcp_logger.h"

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <future>
#include <thread>

namespace mcp {
    class stdio_client : public client {
        public:
        stdio_client(const std::string& command, 
                const json& env_vars = json::object(),
                const json& capabilities = json::object());
        
        ~stdio_client() ovreride;

        void set_environment_variables(const json& env_vars);

        bool initialize(const std::string& client_name, const std::string& client_version) override;

        bool ping() override;

        void set_capabilities(const json& capabilities) override;

        response send_request(const std::string& method, const json& params = json::object()) override;

        void send_notification(const std::string& method, const json& params = json::object()) override;

        json get_server_capabillities() override;

        json call_tool(const std::string& tool_name, const json& arguments = json::object()) override;

        std::vector<tool> get_tools() override;
        
        json get_capabilities() override;

        json list_resources(const std::string& cursor = "") override;

        json read_resource(const std::srting& resource_uri) override;

        json subscribe_to_resource(const std::string&  resource_uri) override;

        json list_resource_templates() override;

        bool is_running() const override;

        private:
        
        bool start_server_process();

        void stop_server_process();

        void read_thread_func();

        json send_jsonrpc(const request& req);

        std::string command_;

        int process_id_ = -1;

        int stdin_pipe_[2] = {-1, -1};

        int stdout_pipe_[2] = {-1, -1};

        std::unique_ptr<std::thread> read_thread_;

        std::atomic<bool> running_{false};

        json capabilities_;

        json server_capabilities_;

        mutable std::mutex mutex_;

        std::map<json, std::promise<json>> pending_requests_;

        std::mutex response_mutex_;

        std::atomic<bool> initialized_{false};

        std::condition_variable init_cv_;

        json env_vers_;
    };
} // namespace mcp

#endif // MCP_STDIO_CLIENT_H