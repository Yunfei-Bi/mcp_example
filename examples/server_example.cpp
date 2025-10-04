#include "mcp_server.h"
#include "mcp_tool.h"
#include "mcp_resource.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>
#include <filesystem>
#include <algorithm>

int main () { 
	std::filesystem::create_directories("./files");

	mcp::server server("localhost", 8888);
	server.set_server_info("ExampleServer", "1.0.0");

	mcp::json capabilities = {
		{"tools", mcp::json::object()}
	};
	server.set_capabilities(capabilities);

	// 注册工具
	mcp::tool time_tool = mcp::tool_builder("get_time")
		.with_description("Get current time")
		.build();

	mcp::tool echo_tool = mcp::tool_builder("echo")
		.with_description("Echo input with optional transformations")
		.with_string_param("text", "Text to echo")
		.with_boolean_param("uppercase", "Convert to uppercase", false)
		.with_boolean_param("reverse", "Reverse the text", false)
		.build();

	mcp::tool calc_tool = mcp::tool_builder("calculator")
		.with_description("Perform basic calcutions")
		.with_string_param("operation", "Operation to perform (add, substract, multiply, divide)")
		.with_number_param("a", "First operand")
		.with_number_param("b", "Second operand")
		.build();

	mcp::tool hello_tool = mcp::tool_builder("hello")
		.with_description("Say hello")
		.with_string_param("name", "Name to say hello to", "World")
		.build();

	server.register_tool(time_tool, get_time_handler);
	server.register_tool(echo_tool, echo_handler);
	server.register_tool(calc_tool, calculator_handler);
	server.register_tool(hello_tool, hello_handler);

	// 启动server
	std::cout << "Starting MCP server at localhost:8888..." << std::endl;
	std::cout << "Press Ctrl+C to stop the server" << std::endl;
	server.start(true);

	return 0;
}
