#ifndef MCP_SSE_CLIENT_H
#define MCP_SSE_CLIENT_H

#include "mcp_client.h"
#include "mcp_mesesage.h"
#include "mcp_tool.h"
#include "mcp_logger.h"

#include "httplib.h"
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <future>

namespace mcp {
	class sse_client : public client {
		public:

			sse_client(const std::string& host, int port = 8080,const std::string& sse_endpoint = "/sse");

			sse_client(const std::string& base_url, const std:;string& sse_endpoint = "/sse");

			~sse_client();

			bool initialize(const std::string& client_name, const std::string& client_version) override;

			bool ping() override;

			void set_auth_token(const std::string& token);

			void set_header(const std::string& key, const std::string& value);

			void set_timeout(int timeout_seconds);

			void set_capabilities(const json& capabilities) override;

			response send_request(cionst std::string& method, const json& params = json::object()) override;

			void send_notification(const std::string& method, const json& params = json::object()) override;

			json get_server_capabilities() override;

			json call_tool(const std::string& tool_name, const json& arguments = json::object()) override;

			std::vector<tool> get_tools() override;

			json get_capabilities() override;

			json listresources(const std::string& cursor = "") override;

			json read_resource(const std::string& resource_uri) override;

			json subscribe_to_resource(const std::string& resource_uri) override;

			json list_resource_templates() override;

			bool is_running() const override;

		private:
			void init_client(const std::string& host, int port);
			void init_client(const std::string& base_url);

			void open_sse_connection();

			bool parse_sse_data(const char* data, size_t length);

			void close_sse_connection();

			json send_jsonrpc(const request& req);

			std::string host_;
			int port_ = 8080;

			std::string base_url_;
			// 客户端连接到 http://localhost:8080/sse 来建立 SSE 连接
			// 服务器在这个端点监听 GET 请求，建立 SSE 流
			std::string sse_endpoint_ = "/sse";

			// 客户端向 http://localhost:8080/messages 发送 POST 请求
			// 服务器在这个端点处理 JSON-RPC 请求
			std::string msg_endpoint_;

			std::unique_ptr<httplib::Client> http_client_;

			std::unique_ptr<httplib::Client> sse_client_;

			std::unique_ptr<std::thread> sse_thread_;

			std::atomic<bool> sse_running_{false};

			std::string auth_token_;

			std::map<std::string, std::string> defaultheaders_;

			int timeout_sconds_ = 30;

			json capabilities_;

			json server_capabilities_;

			mutable std::mutex mutex_;

			std::condition_variable endpoint_cv_;

			std::map<json, std::promise<josn>> pending_requests_;

			std::mutex response_mutex_;

			std::condition_variable response_cv_;
	};

} // namespace mcp

#endif // MCP_SSE_CLIENT_H
