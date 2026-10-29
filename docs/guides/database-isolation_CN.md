# 通过公共插件验证 MySQL 一致性读隔离

`ark_database_public_plugin_isolation_tests` 加载真实的隐藏符号 MySQL 插件动态库，
仅使用公共协程、管理器和任务接口。在服务端显式配置默认隔离级别的自有 MySQL 8.4 服务上，
程序验证 `REPEATABLE READ` 和 `READ COMMITTED` 的一致性读行为。公共 `begin()` 没有隔离级别参数。

## 运行验收程序

按[数据库测试说明](../../tests/database/README_CN.md)配置启用 SSL 的构建，再编译并运行：

```sh
cmake --build database-build --target ark_database_public_plugin_isolation_tests --parallel 1
# 私有服务使用 --transaction-isolation=REPEATABLE-READ 启动：
./database-build/tests/database/ark_database_public_plugin_isolation_tests repeatable-read
# 另一私有服务使用 --transaction-isolation=READ-COMMITTED 启动：
./database-build/tests/database/ark_database_public_plugin_isolation_tests read-committed
```

服务必须使用全新的私有数据目录、专用 schema 和经过认证的账户。账户仅需该 schema
的 CREATE、DROP、SELECT、INSERT 和 UPDATE 权限。要求 TLS，并提供可信 CA。
设置 `ARK_DATABASE_MYSQL_HOST=127.0.0.1`，以及既有的 `ARK_DATABASE_MYSQL_PORT`、
`ARK_DATABASE_MYSQL_USER`、`ARK_DATABASE_MYSQL_PASSWORD`、`ARK_DATABASE_MYSQL_DATABASE`
和 `ARK_DATABASE_MYSQL_CA`。缺少配置、密码为空或 host 不是 `127.0.0.1` 时，程序在连接前失败。
程序不启动或停止服务。成功用例删除唯一命名的 InnoDB 测试表；失败时，只要连接仍可用，
程序会尝试回滚和删表。失败后需清理私有 schema 和服务。

每条命令需连接匹配的私有服务配置。参数声明预期值，不修改服务端或事务配置。程序在两个公共
事务内检查连接 ID、会话隔离级别和全局隔离级别。在 `begin()` 前，程序通过命名查询故意将读连接
的会话隔离级别设为相反值。归还查询租约会执行
[`COM_RESET_CONNECTION`](https://dev.mysql.com/doc/c-api/8.4/en/mysql-reset-connection.html)；后续事务必须在同一连接
上恢复服务端默认值。因此，单独调用 `query()` 设置会话隔离级别，不能跨越该重置边界配置后续的
公共 `begin()`。本验收保留既有 API 和连接池行为。事务配置作用域见
[MySQL 官方说明](https://dev.mysql.com/doc/refman/8.4/en/set-transaction.html)。

两个命名连接池各有一个连接。读事务先建立快照，再由写事务更新已有行并插入新行。
写事务提交前，读事务仍看到原始行，且看不到插入行。写事务提交后，`REPEATABLE READ`
在读事务结束前保留原始快照；`READ COMMITTED` 在同一事务的后续普通 `SELECT` 中看到两项
已提交变更。读事务结束后，两种隔离级别都能看到已提交变更。连接 ID 验证两个会话相互独立，
且每次事务读取仍使用固定连接。

任务首次轮询前，管理器进入自然排空状态。每个续体和唯一一次任务完成都必须位于所有者线程
及共享执行器上。初始快照和事务结束后查询的自有结果，在后续操作、删表以及模块、管理器和
运行时销毁后仍可读取。`check-database` 只编译该程序，不启动服务，也不运行真实服务用例。

## SQL 与验证范围

[MySQL 8.4 隔离级别文档](https://dev.mysql.com/doc/refman/8.4/en/innodb-transaction-isolation-levels.html)
说明：`REPEATABLE READ` 的一致性读使用事务快照；`READ COMMITTED` 的每次一致性读使用新快照。
本程序只验证普通非锁定读取，不覆盖锁定读取、`SERIALIZABLE`、事务取消、崩溃持久性、
重启恢复、复制、高可用、其他平台或生产容量。实现不新增隔离级别 API，也不修改连接池策略或默认隔离级别。

## 本地验收

**2026-10-29**，两种调用方式均通过。实际执行的 ark 源码为
`093dfcb9a430beedcd86de19eb0d70c8aae9160e`，arknet 为
`a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`。补入本记录时，验收程序源码未再变化。
环境为 macOS 27.0.1 arm64、Apple Clang 21.0.0、C++20 Debug、Boost.Asio 1.90.0 和
OpenSSL 3.6.4。每种配置都使用全新的 MySQL Community Server 8.4.13 私有实例，监听
`127.0.0.1:29879`，配置匹配的显式默认隔离级别，并启用认证及可信 TLS/IP 身份校验。
两次验收独立确认的读/写连接 ID 均为 `11`/`10`。

在每个真实服务上，传入相反的预期隔离级别均在会话/全局隔离级别断言处失败。
缺少 host、非 loopback host、空密码、缺少参数和未知隔离级别参数的对照也均按预期失败。
独立清理查询确认剩余测试表为 `0`。两个自有服务都经过认证正常关闭，并以退出码 `0` 回收；
独立检查确认精确 PID、监听端口和私有目录均不存在。私有数据、凭据和 TLS 密钥已删除。
原始日志以及程序和源码哈希仅保留在本地。本记录只证明 macOS Boost Debug 环境下的真实服务验收，
不代表托管 CI 或跨平台验收。
