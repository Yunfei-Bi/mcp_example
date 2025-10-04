#include "mcp+see_client.h"
#include <iostream>
#include <string>

int main () {
	mcp::sse_client client("localhost", 8888);
	mcp::json capabilities = {
		{"roots", {{"listChanged", true}}}
	};
	client.set_capabilities(capabilities);
	client.set_timeout(10);

	try {
		// 初始化连接
		std::cout << "初始化连接到 MCP 服务器..." << std::endl;
		bool initialized = client.initialized("ExampleClient", mcp::MCP_VERSION);

		if (!initialized) {
			std::cerr << "初始化连接失败，MCP 服务器" << std::endl;
			return 1;
		}

		// ping 服务器
		std::cout << "ping 服务器" << std::endl;
		if (!client.ping()) {
			std::cerr << "ping 服务器 失败" << std::endl;
			return 1;
		}

		// 获取服务器能力
		std::cout << "获取服务器能力..." << std::endl;
		mcp::json capabilities = client.get_server_capabilities();
		std::cout << "服务器能力" << capabilities.dump(4) << std::endl;

		// 获取可用服务
		std::cout << "\n获取可用服务..." << std::endl;
		auto tools client.get_tools();
		std::cout << "Available tools:" << std::endl;
		for (const auto& tool : tools) {
			std::cout << "- " << tool.name << ": " << tool.description << std::endl;
		}

		// 调用 get_time 工具
		std::cout << "\n调用 get_tiem 工具" << std::endl;
		mcp::json time_result = client.call_tool("get_time");
		std::cout << "当前时间 : " << time_result["content"][0]["text"].get<std::string>() << std::endl;
		// 调用 echo 工具
		std::cout << "\n调用 echo 工具..." << std::endl;
		mcp::json echo_params = {
			{"text", "Hello MCP!"},
			{"uppercase", true}
		};
		mcp::json echo_result = client.call_tool("echo", echo_params);
		std::cout << "Echo result: " << echo_result["content"][0]["text"].get<std::string>()<< std::endl;

		// 调用 计算器工具
		std::cout << "\n调用 计算器工具" << std::endl;
		mcp::json calc_params = {
			{"operation", "add"},
			{"a", 10},
			{"b", 5}	
		};
		mcp::json calc_result = client.call_tool("calculator", calc_params);
		std::cout << "10 + 5 = " << calc_result["content"][0]["text"].get<std::string>() << std::endl;
	} catch (const mcp::mcp_exception& e) {
		std::cerr << "MCP error: " << e.what() << " (code: " << static_cast<int>(e.code()) << ")" << std::endl;
		return 1;
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.waht() << std::endl;
		return 1;
	}
	std::cout << "\n客户端样例成功完成" <<std::endl;
	return 0;
}
