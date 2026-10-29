# MySQL 协程插件

`mysql_module` 提供具名 arknet 原生连接池、查询、参数化预处理语句和事务，替换 mysql++、驱动管理模块及原有同步 SQL 构造接口。应用需要重新编译插件，将调用迁移到受管理的协程任务。

公开接口的唯一源码是 `src/plugins/mysql/mysql_module.hpp`，与实现和私有插件装配文件同目录。链接 `ark::mysql` 获取配置期复制到构建树的 `ark/mysql/mysql_module.hpp`；即使不构建运行时插件，该映射仍有效。不要仅依赖手写 `-I <源码>/include`。实现类已隐藏到 `.cpp`，原 `mysql_module_impl.hpp` 和插件装配头属于私有实现。宿主、插件和相关消费者需要一起重新构建；安装后的消费方式使用 `find_package(ark CONFIG REQUIRED COMPONENTS mysql)` 和相同的 `ark::mysql` 目标。依赖与可迁移包的说明见[安装指南](../get-started/installation_CN.md#安装并消费-cmake-包)。安装不承诺旧二进制兼容，也不会自动加载运行时模块。

## 配置与查询

```cpp
// 业务模块成员：ark::mysql_module* database_{}; ark::task_group* tasks_{};
// 在 init() 中，插件注册完成后：
database_ = find_module<ark::mysql_module>();
if (!database_) {
    throw std::runtime_error("MySQL plugin is not registered");
}
tasks_ = &get_plugin_manager()->create_task_group(*this);
ark::mysql_options options;
options.pool.connection.host = "db.example.com";
options.pool.connection.username = "app";
options.pool.connection.database = "app";
options.pool.connection.password = load_secret();
options.pool.connection.ca_file = "/etc/app/mysql-ca.pem";
options.pool.max_connections = 8;
options.max_operations = 64;
options.max_input_bytes = 1024 * 1024;
if (!database_->configure("primary", std::move(options))) {
    throw std::runtime_error("MySQL configuration unavailable");
}

// 在 post_init() 中通过 tasks_->start(run(), on_complete) 启动此成员协程一次。
// on_complete 处理 std::exception_ptr，见托管任务指南。
ark::task<> run() {
    auto reply = co_await database_->query("primary", "SELECT id FROM players LIMIT 10");
    if (!reply) {
        // 检查 reply.result.status 的 code、message、transport_error 和 outcome_unknown。
        co_return;
    }
    // 原生结果的行、列数据拥有独立存储。
    std::vector<arknet::mysql::field_value> parameters{std::int64_t{42}};
    auto prepared = co_await database_->execute("primary", "SELECT ?", std::move(parameters));
    // 分别检查执行结果及 statement_close / statement_close_exception。
}
```

配置只能在框架线程显式创建。注册时已设置插件管理器，因此业务配置可以早于数据库模块自身的 `init()`。重名或管理器排空期间新增配置返回 false，空名称或零限额会抛出异常；`pre_shut()` 封闭模块后再配置会抛出 `std::logic_error`。不支持原位替换连接配置。TLS 默认校验证书，按部署配置 CA 和服务名称。MySQL 可选构建目标要求 `ARKNET_ENABLE_SSL=ON` 及 OpenSSL，不链接 mysql++。官方插件不会从应用清单中的 `config` 路径或数据库 XML 预设读取连接选项。该插件的 `config` 应留空，由业务模块通过 `configure()` 传入拥有型选项。另见[构建与运行时启用](build-options_CN.md#原生数据库插件)和[托管任务生命周期](../guides/tasks_CN.md)。

## 事务

```cpp
ark::task<> update_player() {
    auto begun = co_await database_->begin("primary", {std::chrono::seconds(5), std::chrono::seconds(1)});
    if (!begun) { co_return; }
    const auto id = begun.transaction;
    std::vector<arknet::mysql::field_value> values{std::int64_t{42}};
    auto changed = co_await database_->execute(id, "UPDATE players SET score=?", std::move(values));
    if (!changed) {
        auto rolled_back = co_await database_->rollback(id);
        // 检查 rolled_back.transaction_status，包括清理失败的信息。
        co_return;
    }
    auto committed = co_await database_->commit(id);
    // 传输失败可能意味着提交结果未知，不能直接重放写操作。
}
```

事务各步骤始终使用同一条原生连接。递增标识只对签发它的模块实例有效，不持有插件内部对象。一个事务同一时间只能执行一个操作；未知或过期标识、重叠步骤会抛出异常。最初设置的超时覆盖取连接、BEGIN、各步骤及提交，不因新步骤而重置。未结束的事务到期后尝试回滚；`cleanup_timeout` 单独限制回滚清理时间。清理失败或结果未知时丢弃连接，不自动重试或切换连接重放语句。

`mysql_reply` 保留拥有型 `arknet::mysql::query_result` 及独立的预处理语句关闭诊断。`mysql_transaction_reply` 还保留回滚结果、启动异常和事务最终状态。执行成功并不代表清理或提交成功。数据库及传输错误返回原生结果，本地 C++ 异常和协程桥限额异常向调用方传播。取消请求会转交原生操作；语句可能已经执行时保留 `outcome_unknown`。

## 执行、限额与停机

任务在管理器共享的业务执行器上运行，使用其他执行器会在提交原生请求前被拒绝。插件的 `update()` 发布服务完成结果和协程通知，管理器统一轮询一个手动执行器，并在所属线程恢复业务协程；socket IO 和工作线程由 arknet 管理。惰性任务启动前已持有输入，返回的结果在连接池退役后仍有效。

查询桥和事务桥分别最多保留 `max_operations` 条协程通知记录；查询请求和事务分别具有 `max_input_bytes` 的输入保留预算。每个具名池的活跃事务数由 `max_transactions` 限制。原生连接数、等待队列、查询行数/列数/结果字节上限及连接请求队列另行配置。这些是每个池的限额，不代表整个进程的内存预算。

`stop()` 返回 false 时继续驱动主循环。已接收的工作流仍可提交后续数据库步骤，空闲事务完成到期清理。模块准备检查等待查询、事务及其完成回调栈退出，随后宿主停机阶段才封闭入口并通过原生等待接口退役拥有的连接池。退役失败时保留资源并报告停机失败，不据此承诺热更新或任意外部协程的生命周期安全。

## 验证

`tests/database/public_mysql_module.cpp` 使用有界本机 MySQL 协议端点验证公开查询、预处理执行与关闭、TLS、事务提交/回滚、空闲到期清理、取消及执行器校验。启用 `ARK_BUILD_DATABASE_TESTS=ON` 后运行 `ark_database_public_mysql_tests`，或执行 `check-database` 开发回归集。已有 `ark_database_mysql_tests`、`ark_database_mysql_transaction_tests` 需要真实 MySQL 服务及显式环境配置；本机协议端点不能替代真实服务验收，也不证明生产容量。

`ark_database_public_mysql_plugin_tests` 另外编译并加载真实插件动态库，验证宏注册、协程结果投递和宿主停机。该检查不代表操作系统动态库卸载或热更新验收。

`ark_database_public_service_tests` 通过业务任务组和管理器共享运行时调用实际模块实现，覆盖查询与预处理结果、提交/回滚可见性、输入/配置/结果所有权、所属线程恢复、自然排空，以及工作未完成时的宿主调用栈异常展开。`ark_database_public_failure_service_tests` 检查错误密码、不可信 CA 和服务器可见的 `SELECT SLEEP` 截止时间，保留原生错误及 `outcome_unknown`，再通过显式新请求验证恢复。

```sh
bash scripts/test_databases.sh database-build public-mysql
bash scripts/test_databases.sh database-build public-mysql-failures
```

按[数据库测试指南](../../tests/database/README_CN.md)准备隔离服务、TLS 证书验证、环境变量和权限。缺少配置会失败，不会跳过并报告成功。`check-database` 仅编译两个真实服务程序，不运行它们；负面模式需显式选择。这些真实服务用例不覆盖请求取消、服务器终止、动态库卸载或容量。无重放观察窗口仅覆盖带标记的慢请求，不证明一般写操作的重放行为，具体限制见测试指南中的“公开真实服务负面验收”。

测试机器、源码版本与执行结果见[本机公开接口服务验收](../guides/database-validation_CN.md)。

[重启恢复](../guides/database-restart_CN.md)验证原连接池上的显式新查询。[一致性读隔离](../guides/database-isolation_CN.md)比较两种服务端默认级别；`begin()` 使用服务端默认值，独立查询租约的会话设置不会跨越连接池重置保留。
