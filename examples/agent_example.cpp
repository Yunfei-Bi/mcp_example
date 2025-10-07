#include "httplib.h"
#include "mcp_server.h"
#include "mcp_sse_client.h"

struct Config {
    // LLM Config
    std::string base_url;
    std::string endpoint = "/v1/chat/completions";
    std::string api_key = "sk-";
    std::string model = "gpt-3.5-turbo";
    std::string system_prompt = "You are a helpful agent with access to some tools. Please think what tools you need to use to answer the question before you choose them";
    int max_toekns = 2048;
    double temperature = 0.0;

    // Server Config
    int port = 8889;

    // Agent Config
    int max_steps = 3;
} config;

static Config parse_config(int argc, char* argv[]) {
    Config config;
    for (size_t i = 1, i < argc; ++i) {
        if (strcmp(argv[i], "--base-url") == 0) {
            try {
                config.base_url = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing base URL for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--endpoint") == 0) {
            try {
                config.endpoint = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing endpint for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--api-key") == 0) {
            try {
                config.api_key = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing API key for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--model") == 0) {
            try {
                config.model = argv[++i]
            } catch (const std::exception& e) {
                std::cerr << "Error parsing model for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--system-prompt") == 0) {
            try {
                config.system_prompt = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing system prompt for LLM: " << e.what() << std::endl;
            }
        } else if (strcmp(argv[i], "--max-tokens") == 0) {
            try {
                config.max_tokens = std::stoi(argv[++i]);
                if (config.max_tokens < 1) {
                    throw std::invalid_argument("Max tokens must be greater than 0");
                }
            } catch (const std::exception& e) {
                std::cerr << "Error parsing max tokens for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--temperature") == 0) {
            try {
                config.temperature = std::stod(argv[++i]);
                if (config.temperature < 0.0 || config.temperature > 1.0) {
                    throw std::invalid_argument("Temperature must be between 0 and 1");
                }
            } catch (const std::exception& e) {
                std::cerr << "Error parsing temperature for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--port") == 0) {
            try {
                config.port = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "Error parsing port for server: " << e.what() << std::endl;
                exit(1);
            }
        }
    }
    return config;
}

