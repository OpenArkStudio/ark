# 公开数据库插件取消验收

[English](cancellation.md)

`ark_database_public_plugin_cancellation_tests` 通过 DLL 入口加载真实的隐藏符号数据库插件，宿主仅链接公开接口。现有 `check-database` 目标编译该程序，但不启动服务，也不执行真实服务模式。

只使用绑定 `127.0.0.1` 的自有认证服务、全新私有目录和独占键／数据库。运行前及正常认证停服前，核实可执行文件路径、子进程 PID、PID 文件与回环监听。不要对共享服务或生产服务执行这些写入。凭据仅通过私有文件／环境变量传递，不放入命令参数或上传证据。

```sh
cmake -S . -B database-build -G Ninja \
  -DARKNET_SOURCE_DIR=/path/to/the-pinned-arknet \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_CORE_TESTS=OFF -DARK_BUILD_TESTS=OFF \
  -DARK_BUILD_CONFIG=OFF -DARK_BUILD_DATABASE_TESTS=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build database-build --target ark_database_public_plugin_cancellation_tests --parallel 1
# 提供私有 ARK_DATABASE_* 环境变量后运行：
database-build/tests/database/ark_database_public_plugin_cancellation_tests redis
database-build/tests/database/ark_database_public_plugin_cancellation_tests mysql
```

Redis 使用带 `ARK_DATABASE_REDIS_` 前缀的 `HOST`、`PORT`、`PASSWORD`，以及可选的 `USER`。MySQL 使用带 `ARK_DATABASE_MYSQL_` 前缀的 `HOST`、`PORT`、`USER`、`PASSWORD`、`DATABASE`、`CA`。MySQL 要求可信 CA 覆盖该 IP、构建与服务均开启 TLS，并为独立账户设置 SSL 要求；`CREATE`、`INSERT`、`UPDATE`、`SELECT`、`DROP` 权限只授予新建的私有数据库。观察连接属于同一账户，无需全局 PROCESS 权限。

Redis 任务提交一次 `INCR`／`BLPOP` 流水线。独立命名连接池通过 `CLIENT LIST` 确认唯一客户端名称、阻塞标志与命令，并先验证计数为一，再调用现有 `task_group::cancel(id)`。用例要求保留原生 `client_errc::cancelled`、`command_may_have_run=true`、没有人为合成的传输错误；若已有部分回复，回复仍须有效。显式提交新的 PING 验证恢复；带标记的命令须退出，有限观察期内计数仍为一，随后删除独占键。

MySQL 先创建私有单行计数表，任务只执行一次带标记的 `UPDATE ... SET n=n+1 WHERE id=1 AND SLEEP(10)=0`。独立连接池通过 `information_schema.processlist` 中的 `STATE='User sleep'` 确认该语句正在服务端运行，再取消。原结果须保留 `connection_errc::cancelled`、`outcome_unknown=true` 与 `operation_aborted` 传输状态。观察者等待原服务端语句退出后才检查最终影响：零或一均符合未知结果，超过一则未通过不重放检查。这不把取消推断为回滚。显式提交新查询验证恢复，随后删除私有表。

两个模式均要求协程在所属线程／共享执行器继续、根任务恰好完成一次且没有协程异常、托管任务自然退出、管理器与原生资源在有界时间内正常停机、重复停机成功，以及拥有型取消结果在插件／模块／管理器销毁后仍可读取。重复取消未退出任务成功，取消已退出 ID 失败。定时器只降低轮询频率，服务端观察才是命令已运行的证据。本次不增加强制销毁协程帧、`cancel_all`、终止服务模式、自动写重试或生产策略变更。原生关闭成功不证明库映像已从物理地址空间移除。

这些是手动启用的取消场景，与正常排空模式及超时／错误 fixture 分开。此前查询模式的结果不覆盖 Redis TLS、事务取消或全部错误竞争、外部服务互通、跨平台验收、热更新或容量。实际执行的精确源码／运行时身份与清理证据由本地交接记录提供；测试说明本身不代表执行通过。

## MySQL 事务内操作取消

仅在启用 SSL 时可用的 `mysql-transaction` 模式使用既有公共 `begin()` 和
`query(transaction, ...)` 接口，无需新增构建目标：

```sh
database-build/tests/database/ark_database_public_plugin_cancellation_tests mysql-transaction
```

沿用上述 MySQL 私有环境变量和 schema 范围内的权限，密码必须非空。程序创建唯一命名的
InnoDB 计数表。请求先开启事务并记录连接 ID，将计数加到一，再保留事务内读取该未提交值的
成功结果。随后在同一事务内提交带标记的慢 UPDATE。独立观察会话必须确认精确连接及带标记
语句处于 `User sleep`，且仍读取到计数零，之后才取消托管请求任务。

已发送操作的取消结果须保留 `connection_errc::cancelled`、`outcome_unknown=true` 和
`operation_aborted`。事务状态须为 `unconfirmed`、`discard_requested=true`，没有回滚回复或
回滚状态码，也没有清理、取消或租约归还异常。该路径丢弃结果未知的连接，不通过已关闭连接
伪造回滚。调用方不通过 SQL 重启或替换事务，也不重试写入。

通过原命名连接池显式提交的新查询必须在不同的服务端连接上成功。观察者持续跟踪原连接 ID
和带标记 SQL，直到原会话完全消失，随后还需经过 500 ms 的有限观察期，确认原会话或带标记
语句没有再次出现。只有在会话退出后，程序才要求计数为零并删表。这验证本次丢弃连接后观察到
的服务端效果，包括此前已经执行、尚未提交的写入；不会将 API 的未知结果改称已确认回滚，
也不证明持久性。成功读取的自有结果在取消、删表以及模块、管理器和运行时销毁后仍须保留值一，
取消结果仍须保留原始诊断。所属执行器、唯一完成、重复取消和自然退出检查沿用现有约定。

本模式只覆盖一次已发送事务查询的取消，不覆盖获取连接、BEGIN、预处理、执行、语句关闭、
提交或回滚阶段的取消，也不覆盖全部取消竞争、崩溃恢复或自动重试。不重放结论以独立观察窗口
为界。既有 `mysql` 自动提交用例仍按零或一的最终结果判断，本模式不改变该契约。
未启用 SSL 的构建在真实服务操作前拒绝 `mysql-transaction`。

### 本地事务取消验收

**2026-10-29**，`mysql-transaction` 通过。验收程序源码为
`28edd1a88c5e7179668aa387a9158d3db32a15f0`，arknet 固定为
`a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`。环境为 macOS 27.0.1 arm64、
Apple Clang 21.0.0、Boost.Asio 1.90.0、OpenSSL 3.6.4；私有 MySQL Community Server 8.4.13
启用认证和可信 TLS 校验。请求/观察连接 ID 为 `13`/`14`，最终计数为零。独立查询私有服务的
通用日志确认前置写入和带标记慢 UPDATE 各收到一次。原有 `mysql`、`redis` 模式也均通过，
最终计数均为一；Redis 使用另一自有、经过认证的 7.4.2 实例。错误密码、不可信 CA 对照均失败；
未启用 SSL 的构建在真实服务操作前拒绝两个 MySQL 模式。独立查询确认剩余表和键均为零；
两个自有服务正常退出，精确 PID、监听端口、私有数据、凭据和密钥均已清理。
本记录只证明本机 macOS Boost Debug 验收，不代表托管或跨平台检查。
