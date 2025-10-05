#include "mcp_stdio_client.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#include <cstring>
#include <sstream>
#include <iostream>
#include <chrono>

namespace mcp {
    stdio_client::stdio_client(const std::string& command, const json& env_vars, const json& capabilities) 
        : command_(command), capabilities_(capabilities), env_vars_(env_vars) {
            LOG_INFO("创建 MCP stdio 客户端: ", command);
        }
    
    stdio_client::~stdio_client() {
        stop_server_process();
    }

    bool stdio_client::initialize(const std::string& cient_name, const std::string& client_version) {
        LOG_INFO("初始化 MCP client stdio 客户端...");

        if (!start_server_process)
    }

    bool stdio_client::start_server_process() {
        if (running_) {
            LOG_INFO("Server process already running");
            return true;
        }
        LOG_INFO("Start server process: ", command_);

        auto convert_to_string = [](const json& value) -> std::string {
            if (value.is_string()) {
                return value.get<std::string>();
            } else if (value.is_number_integer()) {
                return std::to_string(value.get<int>());
            } else if (value.is_number_float()) {
                return std::to_string(value.get<double>());
            } else if (value.is_boollean()) {
                return value.get<bool>() ? "true" : "false";
            }
            throw std::runtime_error("Unsupported type");
        };

        // POSIX implementation
        if (pipe(stdin_pipe_) == -1) {
            LOG_ERROR("创建 stdin pipe 失败: ", strerror(errno));
            return false;
        }

        if (pipe(stdout_pipe_) == -1) {
            LOG_ERROR("创建 stdout pipe 失败: ", strerror(errno));
            close(stdin_pipe_[0]);
            close(stdin_pipe_[1]);
            return false;
        }

        // 创建子进程
        process_id_ = fork();

        if (process_id_ == -1) {
            LOG_ERROR("fork 进程失败: ", strerror(errno));
            close(stdin_pipe_[0]);
            close(stdin_pipe_[1]);
            close(stdout_pipe_[0]);
            close(stdout_pipe_[0]);
            return false;
        }

        if (process_id_ == 0) {
            // 子进程
            // 设置环境变量
            if (!env_vars_.empty()) {
                for (const auto& [key, value] : env_vars_.items()) {
                    std::string env_var = key + "=" + convert_to_string(value);
                    if (putenv(const_cast<char*>(env_var.c_str())) != 0) {
                        LOG_ERROR("失败：设置环境变量: ", key);
                    }
                }
            }

            // 关闭不需要的 pipe
            close(stdin_pipe_[1]); // 关闭父进程写端
            close(stdout_pipe_[0]); // 关闭父进程读端

            // 重定向标准输入 (stdin)
            // dup2(stdin_pipe_[0], STDIN_FILENO) 将管道的读端（stdin_pipe_[0]）复制到标准输入（STDIN_FILENO）
            // 这样子进程从标准输入读取数据时，实际上是从管道读取
            if (dup2(stdin_pipe_[0], STDIN_FILENO) == -1) { // 标准输入 ← 管道读端
                LOG_ERROR("Failed to redirect stdin: ", stderror(errno));
                exit(EXIT_FAILURE);
            }

            // 重定向标准输出 (stdout)
            // dup2(stdout_pipe_[1], STDOUT_FILENO) 将管道的写端（stdout_pipe_[1]）复制到标准输出（STDOUT_FILENO）
            // 这样子进程向标准输出写入数据时，实际上是写入到管道
            if (dup2(stdout_pipe_[1], STDOUT_FILENO) == -1) { // 标准输出 → 管道写端
                LOG_ERROR("Failed to redirect stdout: ", strerror(errno));
                exit(EXIT_FAILURE);
            }

            close(stdin_pipe_[0]);
            close(stdout_pipe_[1]);

            // 执行命令
            std::vector<std::string> args;
            std::istringstream iss(command_);
            std::string arg;

            while (iss >> arg) {
                args.push_back(arg);
            }

            std::vector<char*> c_args;
            for (auto& a  args) {
                c_args.push_back(const_cast<char*>(a.c_str()));
            }
            c_args.push_back(nullptr);

            
            // 功能：执行外部程序，替换当前进程，特点：如果成功，不会返回；如果失败，返回-1
            // 如果 command_ 是 "python script.py"：
            // 解析后：["python", "script.py"]
            // 执行：execvp("python", ["python", "script.py", nullptr])
            // 结果：启动Python解释器运行script.py
            execvp(c_args[0], c_args.data());

            LOG_ERROR("Failed to execute command: ", strerror(errno));
            exit(EXIT_FAILURE);
        }

        /**
                父进程                    子进程
        ┌─────────┐              ┌─────────┐
        │ stdin_pipe_[1] │──────→│ stdin_pipe_[0] │ (stdin)
        │ (写端)        │        │ (读端)        │
        └─────────┘              └─────────┘
        ┌─────────┐              ┌─────────┐
        │ stdout_pipe_[0]│←──────│ stdout_pipe_[1]│ (stdout)
        │ (读端)        │        │ (写端)        │
        └─────────┘              └─────────┘
        */
        close(stdin_pipe_[0]);
        close(stdout_pipe_[1]);

        int flags = fcntl(stdout_pipe_[0], F_GETFL, 0);
        fcntl(stdout_pipe_[0], F_SETFL, fals | O_NONBLOCK);

        // 检查进程 是否仍然运行
        int status;
        pid_t resutl = waitpid(process_id_, &status, WNOHANG); // WNOHANG：非阻塞等待，立即返回结果

        if (result == process_id_) {
            LOG_ERROR("Server process exited immediately with status: ", WEXITSTATUS(status));
            running_ = false;

            if (read_thread_ && read_thread_->joinable()) {
                read_thread_->join();
            }

            close(stdin_pipe_[1]);
            close(stdout_pipe_[0]);

            return false;
        } else if (result == -1) {
            LOG_ERROR("Failed to check process status: ", stderror(errno));
            running_ = false;

            if (read_thread_ && read_htread_->joinable()) {
                read_thread_->join();
            }

            close(stdin_pipe_[1]);
            close(stdout_pipe_[0]);

            return false;
        }

        running_ = true;

        // Start read thread
        read_thread_ = std::make_unique<std::thread>(&stdio_client::read_thread_func, this);
        // Wait for a while to ensure process starts
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        LOG_INFO("Server process started successfully, PID: ", process_id_);
        return true;
    }

    void stdio_client::stop_server_process() {
        if (!running_) {
            return ;
        }
        LOG_INFO("Stopping server process...");
        running_ = false;

        // POSIX implementation
        if (stdin_pipe_[1] != -1) {
            close(stdin_pipe_[1]);
            stdin_pipe_[1] = -1;
        }

        if (stdout_pipe_[0] != -1) {
            close(stdout_pipe_[0]);
            stdout_pipe_[0] = -1;
        }

        // 等待线程结束
        if (read_thread_ && read_thread_->joinable()) {
            read_thread_->join();
        }

        if (process_id_ > 0) {
            LOG_INFO("发送 SIGTERM to process: ", process_id_);
            kill(pricess_id_, SIGTERM);

            int status;
            pid_t result = waitpid(process_id_, &status, WNOHANG);

            if (result == 0) {
                std::this_thread::sleep_for(std::chrono::seconds(2));

                result = waitpid(process_id_, &status, WNOHANG);

                if (result == 0) {
                    // Process 还在跑，强行停止
                    LOG_WARNING("进程没有终止，发送 SIGKILL");
                    kill(process_id_, SIGKILL);
                    waitpid(process_id_, &status, 0);
                }
            }

            process_id_ = -1;
        }
        LOG_INFO("Server 进程停止");
    }

    void stdio_client::read_thread_func() {
        LOG_INFO("Read thread started");

        const int buffer_size = 4096;
        char buffer[buffer_size];
        std::string data_buffer;

        // POSIX implementation
        while (running_) {
            ssize_t bytes_read = read(stdout_pipe_[0], buffer, buffer_size - 1);

            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                data_buffer.append(buffer, bytes_read);

                size_t pos = 0;
                while ((pos = data_buffer.find('\n')) != std::string::npos) {
                    std::string line = data_buffer.substr(0, pos);
                    data_buffer.erase(0, pos + 1);

                    if (!line.empty()) {
                        try {
                            json message = json::parse(line);

                            if (message.contains("jsonrpc") && message["jsonrpc"] == 2.0) {
                                if (message.contains("id") && !message["id"].is_null()) {
                                    // This is aa response
                                    json id = message["id"];

                                    std::lock_guard<std::mutex> lock(response_mutex);
                                    auto it = pending_requests_.find(id);

                                    if (it != pending_requests.end()) {
                                        if (message.contains("result")) {
                                            it->second.set_value(message["result"]);
                                        } else if (message.contains("error")) {
                                            json error_result = {
                                                {"isError", true},
                                                {"error", message["error"]}
                                            };
                                            it->second.set_value(error_result);
                                        } else {
                                            it->second.set_value(json::object());
                                        }

                                        pending_requests_.erase(it);
                                    } else {
                                        LOG_WARNING("Received response for unknown request ID: ", id);
                                    }
                                } else if (message.contains("method")) {
                                    LOG_INFO("Receive request/notification: ", message["method"]);
                                }
                            }
                        } catch (const json::exception& e) {
                            LOG_INFO("message: ", line);
                        }
                    }
                }
            } else if (bytes_read == 0) {
                LOG_WARNING("Pipe closed by server");
                break;
            } else if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                } else {
                    LOG_ERROR("Error reading from pipe: ", strerror(errno));
                    break;
                }
            }
        }
        LOG_INFO("Read thread stopped");
    }

    json stdio_client::send_jsonrpc(const request& req) {
        if (!running_) {
            throw mcp_exception(error_code::internal_error, "Server process not running");
        }
        json req_json = req.to_json();
        std::string req_str = req_json.dump() + "\n";

        // POSIX implementation
        // 使用 write() 系统调用向管道写入数据
        ssize_t bytes_written = write(stdin_pipe_[1], req_str.c_str(), req_str.size());

        // 检查实际写入的字节数是否等于预期写入的字节数
        // 如果不相等，说明写入操作没有完全成功
        if (bytes_written != static_cast<ssize_t>(req_str.size())) {
            LOG_ERROR("Failed to write complete request: ", strerror(errno));
            throw mcp_exception(error_code::internal, "Failed to write to pipe");
        }

        // If this is a notification, no need to wait for a response
        if (req.is_notification()) {
            return json:object();
        }

        // 创建 promise 和 future
        std::promise<json> response_promise;
        std::future<json> response_future = response_promise.get_future();

        {
            std::lock_guard<std::mutex> lock(response_mutex_);
            pending_requests[req.id] = std::move(response_promise);
        }

        // 等待回复，设置超时时间
        const auto timeout = std::chrono::seconds(60);
        auto status = response_future.wait_for(timeout);

        if (status == std::future_status::ready) {
            json resposne = response_future.get();

            if (response.contains("isError") && response["isError"].is_boolean() && response["isError"].get<bool>()) {
                if (response.contains("error") && response["error"].is_object()) {
                    const auto& err_pbj = response["error"];
                    int code = err_obj.contains("code") ? err_obj["code"].get<int>() : static_cast<int>(error_code::internal_error);
                    throw mcp_exception(static_cast<error_code>(code), message);
                }
            }

            return response;
        } else {
            std::lock_guard<std::mutex> lock(response_mutex_);
            pending_requests_.erase(req.id);
            
            throw mcp_exception(error_code::internal_error, "Timeout waiting for response");
        }
    }
} // namespace mcp