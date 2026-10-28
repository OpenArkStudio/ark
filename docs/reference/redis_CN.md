# Redis 协程插件

`redis_module` 使用 arknet 原生 Redis 连接池，替换原有 bredis 同步接口和回调接口。
迁移时，将 `connect`、`set`、`get` 以及命令专用包装替换为命名连接配置和
`co_await command` / `pipeline`。插件不提供阻塞兼容层，应用需要重新编译。

公开接口的唯一源码是 `src/plugins/redis/redis_module.hpp`，与实现和私有插件装配文件同目录。链接 `ark::redis` 获取配置期复制到构建树的 `ark/redis/redis_module.hpp`；即使不构建运行时插件，该映射仍有效。不要仅依赖手写 `-I <源码>/include`。实现类已隐藏到 `.cpp`，原 `redis_module_impl.hpp` 和插件装配头属于私有实现。宿主、插件和相关消费者需要一起重新构建；安装后的消费方式使用 `find_package(ark CONFIG REQUIRED COMPONENTS redis)` 和相同的 `ark::redis` 目标。依赖与可迁移包的说明见[安装指南](../get-started/installation_CN.md#安装并消费-cmake-包)。安装不承诺旧二进制兼容，也不会自动加载运行时模块。

```cpp
// 业务模块成员：ark::redis_module* redis_{}; ark::task_group* tasks_{};
// 消费模块已注册，在 init() 中配置：
redis_ = find_module<ark::redis_module>();
if (!redis_) {
    throw std::runtime_error("Redis plugin is not registered");
}
tasks_ = &get_plugin_manager()->create_task_group(*this);
ark::redis_options options;
options.pool.connection.host = "127.0.0.1";
options.pool.connection.port = "6379";
options.pool.max_connections = 8;
options.max_operations = 64;
options.max_input_bytes = 1024 * 1024;
if (!redis_->configure("cache", std::move(options))) {
    throw std::runtime_error("Redis configuration unavailable");
}

// 在 post_init() 中通过 tasks_->start(workflow(), on_complete) 启动此成员协程一次。
// on_complete 处理 std::exception_ptr，见托管任务指南。
ark::task<> workflow() {
    arknet::redis::command read{"GET", "key"};
    auto result = co_await redis_->command("cache", std::move(read));
    if (!result) {
        // 检查 result.error、transport_error 和 command_may_have_run。
        co_return;
    }
    // result.replies 拥有字符串、数组和二进制数据；还需检查错误回复。
}
```

配置持有字符串、凭据、连接池、协议、响应限额和 TLS 设置。连接名重复或入口已封闭时，
`configure` 返回 false；空名称和无效限额抛出异常。消费模块可以在 Redis 模块自身的
`init` 之前配置，已安装的管理器会提供任务运行时。官方插件不会从应用清单中的 `config` 路径
或数据库 XML 预设读取连接选项。该插件的 `config` 应留空，由业务模块通过 `configure()`
传入拥有型选项。配置仅限所属线程使用，不支持动态替换。启用
`ARKNET_ENABLE_SSL` 后，可设置 `options.tls = true`，并通过 `options.pool`
配置证书验证；未编译 SSL 时请求 TLS 会抛出异常。另见[构建与运行时启用](build-options_CN.md#原生数据库插件)
和[托管任务生命周期](../guides/tasks_CN.md)。

命令工厂按值接收输入，在惰性任务启动前保存数据。返回任务还会持有连接状态，因此尚未
启动的任务在模块外观对象销毁后仍有有效状态。操作必须运行在管理器的同一个任务执行器
上；外部执行器会在提交原生请求前被拒绝。未知连接名返回原生 `not_connected`，入口
封闭后返回 `closed`。桥接层准入失败抛出异常，无效截止时间返回原生
`invalid_command`。原生传输和数据库错误保留为 `arknet::redis::result`；Redis
错误回复仍是有序 `replies` 向量中的值。结果转换为 true 只表示操作状态成功，
不代表每条 Redis 回复均成功。

`pipeline(name, commands, timeout)` 返回包含有序回复的单个原生结果。超时覆盖连接池
获取和命令执行。`max_operations` 限制桥接记录和服务中等待所属线程交付的操作数量；
`max_input_bytes` 限制服务保留的命令存储，计入已分配容量。输入限额拒绝返回原生
`queue_full`。连接池、等待队列和响应限额分别通过 `options.pool` 配置。这些限额按
命名连接生效，不代表整个进程的内存上限。

管理器托管任务组的取消请求会转交原生服务。等待连接期间取消的请求确定尚未发送；发送
后取消可能返回 `command_may_have_run == true`。插件不会重放写操作，也不会合成成功
结果。调用者持有的原生结果及部分回复在连接池退出后仍有效。

管理器更新期间，Redis 插件先执行 `service.update()`，再调度桥接交付。管理器统一轮询
共享任务运行时，并在所属线程恢复业务协程；arknet 负责原生 IO 和工作线程。停止期间，应用仍需持续更新管理器：
`stop()` 可以在已接收工作流结束前返回 false，同一工作流的后续命令仍可提交。就绪检查
只观察逻辑活动，不会提前封闭服务。所有托管任务和交付调用栈返回后，`pre_shut` 封闭
入口，并在宿主生命周期调用帧中执行原生 `retire_native()`。返回 false 或抛出异常时
保留资源，`shut` 不会释放未退出的服务。`service.stop()` 不是原生退出屏障。运行中的
任务递归调用生命周期函数不构成受支持的销毁路径，管理器会将停机工作延后到宿主调用帧。

`tests/database/public_redis_module.cpp` 使用有界本机 RESP 协议端点验证公开接口：输入
与结果所有权、流水线顺序、外部执行器拒绝、限额、取消、TLS 证书验证和管理器优雅
停止。该端点不是 Redis 实现。底层服务的私有 TLS 回归还覆盖其他失败场景。以上本地
检查不代表真实 Redis
部署验收、生产容量验证或一般性的无泄漏保证。

`ark_database_public_redis_plugin_tests` 另行编译并加载实际插件动态库，验证宏注册、协程结果投递和宿主停机。该检查不代表操作系统动态库卸载、热更新或真实 Redis 服务验收。

`ark_database_public_service_tests` 通过业务任务组和管理器共享运行时调用实际模块实现，覆盖认证
配置、有序二进制流水线、输入/配置/结果所有权、所属线程恢复、自然排空，以及工作未
完成时的宿主调用栈异常展开。`ark_database_public_failure_service_tests` 检查错误密码和服务器可见的 BLPOP 截止
时间，保留原生错误及 `command_may_have_run`，再通过显式新 PING 验证恢复。

```sh
bash scripts/test_databases.sh database-build public-redis
bash scripts/test_databases.sh database-build public-redis-failures
```

按[数据库测试指南](../../tests/database/README_CN.md)准备隔离服务和环境变量。
负面模式还要求非空密码和 `CLIENT LIST` 权限。缺少配置会失败，不会跳过并报告成功。
`check-database` 仅编译两个真实服务程序，不运行它们；负面模式需显式选择。这些用例
不覆盖请求取消、服务器终止、动态库卸载或容量。无重放观察窗口仅覆盖带标记的慢请求，
不证明一般写操作的重放行为，具体限制见测试指南中的“公开真实服务负面验收”。

测试机器、源码版本与执行结果见[本机公开接口服务验收](../guides/database-validation_CN.md)。
原连接池通过显式新命令恢复的流程见[Redis 重启恢复验收](../guides/database-restart_CN.md)。
