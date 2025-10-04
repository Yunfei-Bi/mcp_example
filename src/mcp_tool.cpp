#include "mcp_tool.h"
#include <random>
#include <sstream>

namespace mcp {
	tool_builder::tool_builder(const std::string& name)
		: name_(name) {}
	tool_builder& tool_budiler::with_description(const std::string& description) {
		description_ = description;
		return *this;
	}
	tool_builder& tool_builder::add_param(const std::string& name,
			const std::string& description,
			const std::string& type,
			bool required) {
		json param = {
			{"type", type},
			{"description", description}
		};
		parameters_["properties"][name] = param;

		if (required) {
			required_params_.push_back(name);
		}
		return *this;
	}

	tool_builder& tool_builder::with_string_param(const std::string& name,
			const std::string& description,
			bool required) {
		return add_param(name, description, "string", required);
	}

	tool_builder& tool_builder::with_number_param(const std::string& name, 
			const std::string& description, 
			bool required) {
		return add_param(name, description, "number", required);
	}

	tool_builder& tool_builder::with_boolean_param(const std::string& name, 
			const std::string& description, 
			bool required) {
		return add_param(name, description, "boolean", required);
	}

	tool_builder& tool_builder::with_array_param(const std::string& name, 
			const std::string& description, 
			const std::string & item_type,
			bool required) {
		json param = {
			{"type", "array"},
			{"description", description},
			{"items", {
					  {"type", item_type}
				  }}
		};

		parameters_["properties"][name] = param;

		if (required) {
			requried_params_.push_back(name);
		}
		return *this;
	}

	tool_builder& tool_builder::with_object_param(const std::string& name,
			const std::string& description,
			const json& properties,
			bool required) {
		json param = {
			{"type", "object"},
			{"description", description},
			{"properties", properties}
		};

		parameters_["properties"][name] = param;

		if (required) {
			required_params_.push_back(name);
		}
		return *this;
	}

	tool tool_builder::build() const {
		tool t;
		t.name = name_;
		t.description = description_;

		// schema 就是工具参数的"说明书"，告诉使用者这个工具需要什么参数，每个参数是什么类型，以及哪些参数是必需的
		json schema = parameters_;
		schema["type"] = "object";

		if (!required_params_.empty()) {
			schema["required"] = required_params_;
		}

		t.parameters_schema = schema;

		return t;
	}
} // namespace mcp
