#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include "mcp_message.h"
#include "mcp_tool.h"
#include "mcp_logger.h"

#include <string>
#include <vector>
#include <memory>

namespace mcp {
	class client {
		public:
			virtual ~client() = default;

			virtual bool initialize(const std::string& client_name, const std::string& client_version) = 0;

			virtual bool ping() = 0;

			virtual void set_capabilities(const json& capabilities) = 0;

			virtual response send_request(const std::string& method, const json& params = json::object()) = 0;

			virtual void send_notification(const std::string& method, const json& params = json::object()) = 0;

			virtual json get_server_capabilities() = 0;

			virtual json call_tool(const std::string& tool_name, const json& arguments = json::object()) = 0;

			virtual std::vector<tool> get_tools() = 0;

			virtual json get_capabilities() = 0;

			virtual json list_resources(const std::string& cursor = "") = 0;

			virtual json read_resource(const std::string& resource_uri) = 0;

			virtual json subscribe_to_resource(const std::string& resource_uri) = 0;

			virtual json list_resource_templates() = 0;

			virtual bool is_running() const = 0;
	};
} // namespace mcp

#endif // MCP_CLIENT_H
