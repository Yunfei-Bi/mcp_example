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