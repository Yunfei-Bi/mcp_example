#### ✅ 典型 fork + exec 模式
```cpp
pid_t pid = fork();

if (pid == 0) {
    // 子进程：执行外部程序
    std::vector<char*> args = { "ls", "-l", nullptr };
    execvp(args[0], args.data());
    perror("execvp failed");  // 如果执行到这里说明出错
    exit(1);
} else if (pid > 0) {
    // 父进程：等待子进程结束
    waitpid(pid, nullptr, 0);
}
```
👉 这样可以在父进程中保留控制，而不是让当前进程被替换。

#### waitpid 返回值说明
> 0：子进程已结束，返回值是进程ID
0：子进程仍在运行
-1：调用失败

#### pipe
```bash
                父进程                    子进程
        ┌─────────┐              ┌─────────┐
        │ stdin_pipe_[1] │──────→│ stdin_pipe_[0] │ (stdin)
        │ (写端)        │        │ (读端)        │
        └─────────┘              └─────────┘
        ┌─────────┐              ┌─────────┐
        │ stdout_pipe_[0]│←──────│ stdout_pipe_[1]│ (stdout)
        │ (读端)        │        │ (写端)        │
        └─────────┘              └─────────┘
```

#### std::async(launch_policy, callable);
```cpp
launch_policy：启动方式

std::launch::async → 新线程立即执行

std::launch::deferred → 延迟执行，调用 future.get() 时才运行

callable：任何可调用对象（函数、lambda、functor）
```

#### C++ 可变参数模板 + 完美转发（perfect forwarding） 的经典递归写法
template<typename T, typename... Args>
void log_impl(std::stringstream& ss, T&& arg, Args&&... args) {
    ss << std::forward<T>(arg);
    log_impl(ss, std::forward<Args>(args)...);
}

递归展开过程举例

假设你调用：

```cpp
std::stringstream ss;
log_impl(ss, 42, " hello ", 3.14);
```

编译器展开调用序列如下：

```cpp
// 第一次展开：
T = int
Args... = { const char*, double }
→ ss << 42;
→ log_impl(ss, " hello ", 3.14);

// 第二次展开：
T = const char*
Args... = { double }
→ ss << " hello ";
→ log_impl(ss, 3.14);

// 第三次展开：
T = double
Args... = { }   // 空参数包
→ ss << 3.14;
→ log_impl(ss); // （通常这里会有个重载函数作为递归终止条件）
```
递归终止条件（一般配套写法）

```cpp
void log_impl(std::stringstream& ss) {
    // 递归出口，不做任何事
}
```
这样最后一次调用就会停止，不再递归。

#### 每个宏的展开效果

```cpp
#define LOG_DEBUG(...) mcp::logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...) mcp::logger::instance().info(__VA_ARGS__)
#define LOG_WARNING(...) mcp::logger::instance().warning(__VA_ARGS__)
#define LOG_ERROR(...) mcp::logger::instance().error(__VA_ARGS__)
```

以 LOG_INFO(...) 为例：

定义：
```cpp
#define LOG_INFO(...) mcp::logger::instance().info(__VA_ARGS__)
```

假设你在代码中写：
```cpp
LOG_INFO("Server started on port ", 8080);

```
相当于提供了一层“语法糖”封装，让你用更短的写法
预处理器会在编译前自动替换成：
```cpp
mcp::logger::instance().info("Server started on port ", 8080);
```

#### 宏里的 ... 和 __VA_ARGS__ 是什么？

```bash
... 表示 可变参数宏（variadic macro）
—— 它允许宏接受任意数量的参数。
__VA_ARGS__ 是一个宏关键字，代表传入的所有参数内容。
```
