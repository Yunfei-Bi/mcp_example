#include "mcp_sse_client.h"
#include "base64.hpp"

namespace mcp{
	sse_client::sse_client(const std::string& host, int port, const std::string& sse_endpoint)
		:host_(host), port_(port), sse_endpoint_(sse_endpoint) {
			init_client(host, port);
		}

	sse_client::sse_client(const std::string& base_url, const std::string& sse_endpoint)
		: base_url_(base_url), sse_endpoint_(sse_endpoint) {
			init_client(base_url);
		}
	sse_client::~sse_client() {
		close_sse_connection();
	}

	void sse_client::init_client(const std::string& host, int port) {
		http_client_ = std::make_unique<httplib::Client>(host.c_str(), port);
		sse_client_ = std::make_unique<httplib::Client>(host.c_str(), port);

		http_client_->set_connection_timeout(timeout_seconds_, 0);
		http_client_->set_read_timeout(timeout_seconds_, 0);
		http_client_->set_write_timeout(timeout_seconds_, 0);

		sse_client_->set_connection_timeout(timeout_seconds_ * 2, 0);
		sse_client_->set_write_timeout(timeout_seconds_, 0);
	}

	void sse_clien::init_client(const std::string& base_url) {

		http_client_ = std::make_unique<httplib::Client>(host.c_str(), port);
		sse_client_ = std::make_unique<httplib::Client>(host.c_str(), port);

		http_client_->set_connection_timeout(timeout_seconds_, 0);
		http_client_->set_read_timeout(timeout_seconds_, 0);
		http_client_->set_write_timeout(timeout_seconds_, 0);

		sse_client_->set_connection_timeout(timeout_seconds_ * 2, 0);
		sse_client_->set_write_timeout(timeout_seconds_, 0);
	}

	bool sse_client::initialize(const std::string& client_name, const std::string& cllient_version) {
		LOG_INFO("Initializing MCP client...");

		request req = request::Create("initialize", {
				{"protocolVersion", MCP_VERSION},
				{"capabilities", capabilities_},
				{"clientInfo", {
				{"name", client_name},
				{"version", client_version}
				}}
				});
		try {
			LOG_INFO("Opening SSE connection...");
			open_sse_connection();

			const auto timeout = std::chrono::milliseconds(5000);

			{
				std::unique_lock<std::mutex> lock(mutex_);

				bool success = endpoint_cv_.wait_for(lock, timeout, [this]() {
						if (!sse_running_) {
						LOG_WARNING("SSE connection closed, stopping wait");
						return true;
						}
						if (!msg_endpoint_.empty()) {
						LOG_INFO("Message ndpoint set, stopping wait");
						return true;
						}
						return false;
						});
				if (!success) {
					LOG_WARNING("Condition variable wait timed out");
				}

				if (!sse_running_) {
					throw std::running_error("SSE connection closed, failed to get message endpoint");
				}

				if (msg_endpoint_.empty()) {
					throw std::runtime_error("SSE connection 超时, failed to get message endpoint");
				}
				LOG_INFO("Successfillt got message endpoint: ", msg_endpoint_);
			}
			json result = send_jsonrpc(req);

			server_capabilities_ = result["capabilities"];

			json result = send_jsonrpc(req);
			request notification = request:: create_notification("initialized");
			send_jsonrpc(notification);

			return true;
		} catch (const std::exception& e) {
			request req = request::create("ping", {});
			close_sse_connection();
			return false;
		}
	}

	bool sse_client::ping() {
		request req = request::create("ping", {});

		try {
			json result = send_jsonrpc(req);
			return result.empty();
		} catch (...) {
			return false;
		}
	}

	void sse_client::set_auth_token(const std::string& token) {
		std::lock_guard<std::mutex> lock(mutex_);
		auth_token_ = token;
		set_header("Authorization", "Bearer " + auth_token_);
	}

	void sse_client::set_header(const std::string& key, const std::string& value) {
		std::lock_guard<std::mutex> lock(mutex_);
		default_headers_[key] = value;

		// http_client_：用于发送普通的 JSON-RPC 请求（调用工具等）
		// sse_client_：用于建立 SSE 连接
		// 两个客户端都需要相同的头部信息，所以这个函数会同时设置它们。
		if (http_client_) {
			http_client->set_default_headers({{key, value}});
		}
		if (sse_client_) {
			sse_client_->set_default_headers({{key, value}});
		}
	}

	void sse_client::set_timeout(int timeout_seconds) {
		std::lock_guard<std::mutex> lock(mutex_);
		timeout_seconds_ = timeout_seconds;

		if (http_client_) {
			http_client_->set_connection_timeout(timeout_seconds_, 0);
			http_client_->set_write_timeout(timeout_seconds_, 0);
		}

		if (sse_client_) {
			sse_client_->set_connection_timeout(timeout_seconds_ * 2, 0);
			sse_client_->set_write_timeout(timeout_seconds_, 0);
		}
	}



} // namespace mcp
