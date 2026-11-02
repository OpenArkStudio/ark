# 协程任务

`<ark/task.hpp>` 提供 `ark::task<T>`（Asio `awaitable<T>` 别名，无自定义 promise）和 `ark::task_group`。组启动 `task<void>` 顶层任务，可等待 `task<T>` 的拥有型返回值或兼容的 Asio 操作。模块共用 `plugin_manager` 的执行器和模块任务组；独立工具可自行创建。

## 构建并运行示例

按[安装与构建](../get-started/installation_CN.md)配置依赖后，在 macOS 或 Linux 的 ark 源码目录运行：

```sh
cmake -S . -B task-build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_CONFIG=OFF -DARK_BUILD_TESTS=OFF \
  -DARK_BUILD_CORE_TESTS=ON -DARK_BUILD_EXAMPLES=ON
cmake --build task-build --target ark_task_example check-task-sdk
./task-build/examples/task_group/ark_task_example
```

PowerShell 使用相同选项，写成单行：

```powershell
cmake -S . -B task-build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF -DARK_BUILD_CONFIG=OFF -DARK_BUILD_TESTS=OFF -DARK_BUILD_CORE_TESTS=ON -DARK_BUILD_EXAMPLES=ON
cmake --build task-build --target ark_task_example check-task-sdk
.\task-build\examples\task_group\ark_task_example.exe
```

按需补充项目的依赖 toolchain/prefix 选项。默认使用 Boost.Asio；核心任务构建也支持 `-DASIO_STANDALONE=ON`。`ARK_BUILD_EXAMPLES=ON` 在关闭应用时仍构建此程序，无须插件动态库、生成的 Protobuf 消息或数据库服务。

[完整示例](../../examples/task_group/main.cpp)输出 `result=42 completed=1`：启动一个协程，停止顶层任务准入，再轮询至完成。两秒失败检查以代码 2 退出，避免销毁待完成协程；它不是应用停机超时或放弃任务的策略。

CMake 项目已加入 ark 时，使用公开目标：

```cmake
target_link_libraries(my_tasks PRIVATE ark::core)
```

`ark::core` 传递 C++20、公开头文件路径和 arknet 依赖要求，无须私有 `src/plugins` 路径。任务仍实现在公开头文件中。

## 在所属线程启动有返回值的任务

子协程可以挂起并返回一个值：

```cpp
#include <ark/task.hpp>

ark::task<int> next_value(int value) {
    co_await asio::post(asio::use_awaitable);
    co_return value + 1;
}

ark::task<> compute(int& result) {
    result = co_await next_value(41);
}
```

示例的 `main()` 创建共享的 `arknet::manual_runtime` 和任务组，再提交 `compute(result)`。完成回调接收 `std::exception_ptr`（成功时为空）。被引用状态须存活到任务结束；协程不得在 lambda 闭包销毁后继续引用其捕获。宜用具名协程函数，明确参数所有权。

runtime 和组须在同一线程构造、操作和销毁。`manual_runtime` 不创建执行任务或恢复协程的工作线程；`poll()` 在构造线程执行就绪的 Asio handler。定时器或名称解析可能使用内部辅助线程；原生数据库或网络池仍遵循各自的 IO 所有权约定。

## 模块共用业务执行器

![模块任务与共享执行器](../assets/shared-business-runtime_CN.svg)

每个管理器拥有一个网络库提供的 `manual_runtime`。注册模块后，在初始化时调用 `manager.create_task_group(module, maximum)`，再通过 `manager.get_tasks(module)` 提交。上限须大于零；未注册模块、重复创建或关闭期间创建都会被拒绝。每个模块指针只能注册一次；后续注册不影响已有组引用。

私有数据库桥接通过 `manager.get_task_runtime()` 共用此执行器，公开 MySQL 和 Redis 插件也在内部使用。按以下顺序驱动：

1. 模块 `update()`：先 `service.update()`，再 `bridge.dispatch()`。
2. `manager.update(timestamp, maximum_handlers = 64)`：统一轮询一次，所有组共用 handler 预算。模块不得另行轮询组或 runtime。

`manager.update()` 在服务完成派发后统一轮询共享执行器。`manager.is_polling_tasks()` 仅查询轮询阶段，不授予业务请求或 HTTP 续接权限。独立任务组仍可使用 `task_group::poll()`；模块任务由管理器驱动，避免重复轮询。

`manager.stop(timestamp)` 进入 `stopping`，对全部任务组调用 `close()`，关闭准入并请求取消。有任务或活跃回调栈时返回 `false`，不强制销毁帧。继续所属线程更新与原生完成派发；取消是否生效取决于等待的操作。框架不等待业务流程自然结束，也不保证写操作未发生。公告、关入口、踢人和保存须在停止进程前完成。

任务与活跃栈结束，全部模块 `ready_to_shut()` 为真后，先按反向依赖执行全部 `pre_shut()`，在所有对象存活时关闭原生回调源；再反向逐个执行 `shut()` 并销毁拥有的对象，之后注销插件，最后关闭全部动态库。消费者销毁前提供方仍存活。钩子可能阻塞；handler 数量预算不限制停机耗时。

`manager.stop_until(timestamp, deadline)` 接收 `std::chrono::steady_clock::time_point`。最早的截止时间持续生效，后续无期限 `stop()` 不能清除。默认不设置截止时间。检查发生在就绪检查与同步清理边界，不能中断阻塞调用或回滚已完成的动作。`get_shutdown_failure()` 保留阶段／模块，普通超时的 `get_shutdown_exception()` 为空。见[启动器超时行为](applications_CN.md#确认启动并停止进程)。

| 状态 | 调用方处理 |
| --- | --- |
| `running` | 正常更新并受理任务 |
| `stopping` | 任务组已请求取消；继续 owner 更新，等待帧、活跃栈与原生资源安全 |
| `timed_out` | 清理前到达可选截止时间；继续 owner 更新，但不恢复清理 |
| `cleanup_timed_out` | 清理开始后超时；保留剩余资源，停止后续更新与清理 |
| `failed` | 清理／就绪／原生库关闭失败；保留剩余资源，不重试后续清理 |
| `stopped` | 资源清理与原生关闭成功；重复 `stop()` 返回 `true` |

通过 `get_shutdown_state()`、`get_shutdown_failure()` 和 `get_shutdown_exception()` 区分等待与失败。没有默认业务排空期限，也不通过分离线程或销毁待完成帧结束停机。

| `dynamic_library::unload()` 结果 | 效果 |
| --- | --- |
| 原生关闭失败 | 返回 `false` 并保留句柄；管理器记录 `unload: <plugin>`，保持失败状态，保留包装对象和其余库 |
| 原生关闭成功 | 清空句柄 |
| 空/已关闭包装对象 | 返回 `true`，不调用原生 API |

后续 `stop()` 不重试插件出口/关闭，也不回滚停机工作。关闭成功不证明操作系统已移除库映像。

在构造线程操作管理器和组；更新、关闭钩子或执行器回调中，禁止增删模块或递归更新。有待完成任务时禁止注销任何模块，任务可能借用其他服务提供方。管理器在仍有任务、回调栈中或跨线程析构时终止进程；析构不能发起资源停机。

<Aside type="caution" title="任务完成不等于 IO 资源已释放">

任务检查仅覆盖管理器组已受理的顶层任务。各模块的就绪与资源清理有各自的原生工作范围；参见 [Net HTTP 客户端](../reference/network_CN.md#http-客户端资源清理)与 [HTTP 服务端](../reference/http_CN.md)。安全卸载还需外部回调返回、连接池线程退出、后台清理完成；任务归零不证明这些条件。未受组管理的工作不得引用即将销毁的模块。

</Aside>

## API 与任务准入

| API | 行为 |
| --- | --- |
| `task_group(runtime, maximum = 64)` | 持有共享 runtime，限制已受理顶层任务数；runtime 为空或上限为零时抛出 `std::invalid_argument` |
| `start(task<>, completion = {})` | 返回非零观察/取消 ID；已满/停止准入时返回零，被拒绝的惰性协程不启动、不调用完成回调 |
| `stop_accepting()` | 停止受理新顶层任务，不取消/轮询已受理任务；所属线程可重复调用 |
| `cancel(id)` / `cancel_all()` | 请求 terminal cancellation；持有任务至实际完成 |
| `close()` | 停止准入并请求取消；不轮询、不等待 |
| `poll(maximum = 64)` | 执行至多指定数量的共享 runtime 就绪 handler，不等待；零表示不执行 |
| `pending()` | 已受理且未移除完成记录的顶层任务数 |
| `closed()` | 仅停止准入且所有已受理顶层任务完成时为真 |

丢弃 ID 不取消任务；丢弃未启动的 `task<T>` 不执行协程体。任务体异常由可选完成回调作为失败传递；完成回调的异常从 `poll()` 抛出。

共享 runtime 的轮询可能执行其他组或操作的 handler。上限只计 handler，不计协程步骤或耗时，不保证帧耗时、钩子或停机耗时。禁止重入轮询。跨线程调用抛出 `std::logic_error`；析构要求更严格，见下文。

## 任务完成后再销毁所属对象

| 目的 | 操作与持续驱动要求 |
| --- | --- |
| 独立任务组自然完成 | `stop_accepting()` 后继续所属线程更新；保持服务和桥接存活并受理后续异步操作 |
| 取消 | `close()` 后持续驱动服务完成通知、桥接分发和 runtime 轮询，直至实际完成 |

能否取消取决于异步操作和协程代码。取消请求不强行销毁协程帧，也不证明远端写入未发生。

<Aside type="caution" title="任务组销毁前必须排空">

在构造线程且 `pending() == 0` 时才能销毁组，否则调用 `std::terminate`。析构不分离任务、递归轮询或等待。runtime、异步操作所属对象和被引用状态须存活至不再被需要。

</Aside>

最后一个任务先移除组内记录，再调用用户完成代码；此时可销毁空组对象，但不能销毁仍有其他操作的对象。`closed()` 仅跟踪本组顶层任务，不证明传输层排空、回调屏障、动态库安全卸载或模块停机设计完整。

所有插件须使用同一版本的管理器头文件重新构建；不保证稳定的插件二进制 ABI 或热更新兼容性。

## 继续阅读

- [创建模块](modules_CN.md)：生命周期回调与借用的模块接口。
- [创建插件](plugins_CN.md)：插件注册、构建与运行时选择。
- [数据库协程测试](../../tests/database/COROUTINES_CN.md)：内部原生操作桥接、所属线程更新顺序与显式测试清理。
- [MySQL](../reference/mysql_CN.md) 与 [Redis](../reference/redis_CN.md)：公开插件配置、共享业务执行器与原生连接池退役。

直接继承调度器基类的代码应改用 `ark::scheduler_task`。`ark::task<T>` 表示 Asio 协程；`ark::scheduler` 的调度接口保持原有签名。
