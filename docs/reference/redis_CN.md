# Redis 协程插件

`redis_module` 使用 arknet 原生 Redis 连接池，替换原有 bredis 同步接口和回调接口。
迁移时，将 `connect`、`set`、`get` 以及命令专用包装替换为命名连接配置和
`co_await command` / `pipeline`。插件不提供阻塞兼容层，应用需要重新编译。

| 接口使用 | 契约 |
| --- | --- |
| 唯一源码 | `src/plugins/redis/redis_module.hpp`，与实现和私有装配共置 |
| 构建 | `ark::redis` 在配置期映射 `ark/redis/redis_module.hpp`，运行时插件关闭时仍可用；仅手写 `-I <源码>/include` 不足 |
| 私有实现 | 具体类仅在 `.cpp`；`redis_module_impl.hpp` 与插件装配头保持私有 |
| 安装消费 | `find_package(ark CONFIG REQUIRED COMPONENTS redis)`，仍链接 `ark::redis`；依赖与可迁移包见[安装指南](../get-started/installation_CN.md#安装并消费-cmake-包) |

宿主、插件与受影响消费者须一起重建。安装不承诺旧二进制兼容，也不加载运行时模块。

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

配置拥有字符串、凭据、连接池、协议、响应限额及 TLS 设置，仅限所属线程，不动态替换。`configure` 在重名／封闭准入时返回 false，空名称／无效限额抛异常；可早于模块 `init`，已安装 manager 提供运行时。插件不读取清单 `config` 或数据库 XML；保持 `config` 为空，由业务模块 `configure()` 传入拥有型选项。启用 `ARKNET_ENABLE_SSL` 后用 `options.tls = true` 及 `options.pool` 配置证书验证；无 SSL 构建请求 TLS 会抛异常。见[构建与运行时启用](build-options_CN.md#原生数据库插件)和[托管任务生命周期](../guides/tasks_CN.md)。

命令工厂持有惰性启动前的输入；返回任务保留连接状态，外观对象在未启动任务使用前销毁也不丢失状态。

| 请求边界 | 结果 |
| --- | --- |
| 执行器 | 必须是 manager 的同一个任务执行器；外部执行器在原生提交前抛异常 |
| 未知连接／封闭准入 | 原生 `not_connected`／`closed` |
| 桥接准入／无效截止时间 | 前者抛异常，后者返回原生 `invalid_command` |
| 原生错误／回复 | 保留 `arknet::redis::result`，错误回复仍为有序 `replies` 中的值；true 只表示操作状态，不保证每条回复成功 |

`pipeline(name, commands, timeout)` 返回包含有序回复的单个原生结果。超时覆盖连接池
获取和命令执行。`max_operations` 限制桥接记录和服务中等待所属线程交付的操作数量；
`max_input_bytes` 限制服务保留的命令存储，计入已分配容量。输入限额拒绝返回原生
`queue_full`。连接池、等待队列和响应限额分别通过 `options.pool` 配置。这些限额按
命名连接生效，不代表整个进程的内存上限。

管理器任务组的取消转交原生服务。等待取连接时取消确定未发送，发送后可能 `command_may_have_run == true`；不重放写入、不合成成功。调用者拥有原生结果及部分回复，连接池退役后仍有效。

| 更新／停机 | 契约 |
| --- | --- |
| 完成交付 | 插件先 `service.update()` 再桥接派发；manager 中央轮询共享运行时，在所属线程恢复协程；arknet 负责原生 IO／工作线程 |
| 排空 | `stop()` 返回 false 时继续更新；已接收流程可提交后续命令，就绪仅观察逻辑活动，不提前封闭服务 |
| 原生退役 | 任务及交付栈返回后，宿主生命周期帧中由 `pre_shut` 封闭准入并 `retire_native()`；false／异常保留资源，阻止 `shut` 释放；`service.stop()` 不是退役屏障 |
| 重入 | 不支持任务内递归生命周期销毁，manager 将停机延至宿主帧 |

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
