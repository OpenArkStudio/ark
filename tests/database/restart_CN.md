# 数据库重启恢复操作手册

[English](restart.md)

此可选 C++ 程序通过公开 API 加载真实 Redis 或 MySQL 插件 DSO。`check-database` 只编译
程序，不启动程序或连接服务。测试全程保留原具名连接池，恢复时显式提交新的只读命令，
不重放已中断的操作。恢复行为和本地结果见[简短指南](../../docs/guides/database-restart_CN.md)。

## 构建与私有服务要求

按[数据库测试配置](README_CN.md#构建)构建：

```sh
cmake --build database-build \
  --target ark_database_public_plugin_restart_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_restart_tests redis
# 启用 ARKNET_ENABLE_SSL=ON 时：
./database-build/tests/database/ark_database_public_plugin_restart_tests mysql
```

为新建的私有认证服务设置 `ARK_DATABASE_REDIS_HOST=127.0.0.1`、显式的
`ARK_DATABASE_REDIS_PORT` 和非空的 `ARK_DATABASE_REDIS_PASSWORD`。程序拒绝其他主机。
程序需要与独立启动的[自有服务故障注入脚本](server_termination_CN.md)
共用 `ARK_DATABASE_FAULT_DIR`、`ARK_DATABASE_FAULT_TOKEN`、所有权标记及服务身份记录。
按该指南创建新的 `/tmp/ark-dso-termination-owned.*` 目录，使用规范化的可执行文件路径、
私有配置/数据/PID 文件，并验证认证就绪状态和指定 PID 的监听端口。关闭 Redis 持久化
（`save ""`、`appendonly no`）。程序和故障注入脚本均不安装或发现服务。禁止使用共享服务
或应用数据。现有故障注入脚本依赖 Unix 进程工具，不提供 Windows 故障注入器。

## 受控重启步骤

1. 启动自有服务，记录 PID、启动时间、可执行文件、数据目录和监听端口身份，然后并发
   启动现有故障注入脚本与本 C++ 程序。
2. 程序验证 PING 和唯一 stream key 不存在，然后发出只读请求
   `XREAD BLOCK 60000 STREAMS <unique-key> $`，客户端期限为 20 秒。另一个公开观察池
   必须看到具名客户端、`cmd=xread`、阻塞标记和服务端 client ID，才发布 `redis.ready`。
3. 故障注入脚本再次验证自有进程身份，仅向该 PID 发送 `SIGKILL`，并发布 `redis.fired`。
   编排 shell 必须回收原服务子进程，确认退出码 137，并验证该 PID 和监听端口已消失。
4. 中断请求必须返回 `disconnected`、原生传输错误、`command_may_have_run=true`，且无回复；
   取消和超时均不通过验收。服务仍停止时，通过 `primary` 新发出的 PING 必须返回
   `connection_failed`、传输错误及 `command_may_have_run=false`。程序仅在这些断言通过后，
   原子发布包含 token 的 `redis.down`。
5. 等待 `redis.down` 并验证 token。再次确认端口无人监听，使用同一私有配置在同一端点
   启动服务，记录替换进程的 PID。确认新旧 PID 不同，并验证新 PID 的可执行文件、启动时间、
   数据目录、PID 文件、监听端口和认证 PONG。通过新的 `redis.restarted.tmp` 文件原子发布
   包含 token 的 `redis.restarted`。不得用定时等待代替就绪验证。
6. 程序通过原 `primary` 池提交新的 PING 和二进制 ECHO，两者均须成功。恢复工作流已接收
   但仍待执行时启动关闭；任务必须自然排空，每个根任务在所有者共享 executor 上恰好完成一次。
   manager 须达到 stopped、无待处理任务，重复 stop 须成功。manager/runtime/DSO 销毁后，
   结果仍须有效。
7. 确认程序和故障注入脚本均退出零。通过认证的正常关闭停止替换进程并回收子进程。验证两个
   精确 PID 和监听端口均不存在，再删除自有目录中的凭据、数据和全部协调标记。失败路径也
   必须使用有界的 supervisor 和校验进程身份的清理 trap。

C++ 程序中的等待均有界。观察轮询仅用于限速。shell 拥有服务重启和进程操作权限；
程序只发布观察结果并读取私有协调标记。禁止复用标记。程序不创建 key 或 stream，
不写入应用数据。

## 使用可信 TLS 验证 MySQL 重启

可选的 `mysql` 模式要求 `ARKNET_ENABLE_SSL=ON`；不传参数时仍运行 Redis 模式。
设置 `ARK_DATABASE_MYSQL_HOST=127.0.0.1`、显式的 `ARK_DATABASE_MYSQL_PORT`、
`ARK_DATABASE_MYSQL_USER`、非空的 `ARK_DATABASE_MYSQL_PASSWORD`、
`ARK_DATABASE_MYSQL_DATABASE` 和 `ARK_DATABASE_MYSQL_CA`。使用新建的 MySQL 8.4
数据目录、私有 schema，以及强制 TLS 的只读 SELECT 账号。可信证书必须包含 loopback IP。
关闭 MySQL X Protocol，仅绑定自有 IPv4 loopback 监听端口。沿用上述故障注入脚本的私有
所有权与进程身份检查，使用 `mysql` 标记，并遵守故障指南中的 MySQL 配置要求。

1. 验证认证 TLS 就绪状态。C++ 程序先在 `primary` 和 `observer` 上执行 `SELECT 1`，
   再提交 `SELECT SLEEP(60) /*<unique-tag>*/`，客户端期限为 20 秒。独立的公开观察池必须
   在 `information_schema.processlist` 中看到标记、连接 ID 和 `User sleep` 状态，
   才发布 `mysql.ready`。
2. 故障注入脚本再次检查自有 PID，仅向该 PID 发送 `SIGKILL`。回收原服务子进程，确认退出码为
   137，并验证 PID 和监听端口均不存在。中断结果必须包含原生 `transport`、
   `outcome_unknown=true`、传输错误，且无行或服务端错误。超时和取消均不通过验收。
3. 停服时新提交的 `SELECT 1` 必须返回 `transport`、原生 `connection_refused`、
   `outcome_unknown=false`，且无行或服务端错误。程序仅在这些断言通过后发布 `mysql.down`。
4. 验证 token 和空监听端口。使用同一私有数据目录与配置在同一端点重启。确认替换进程与原
   PID 不同，验证自有进程身份和认证 TLS 就绪状态，再原子发布 `mysql.restarted`。
5. 调用方通过原 `primary` 池提交新的
   `SELECT CAST(42 AS SIGNED), CONCAT('restart', CHAR(0), 'binary')`。
   验证整数与包含 NUL 的字符串、已接收任务的自然排空、所有者 executor 上恰好一次的完成，
   以及重复 stop 成功。DSO 销毁后，再次校验两个原生错误结果和拥有存储的恢复结果行。
6. 确认程序退出零，正常关闭并回收替换服务，验证两个 PID 和监听端口均不存在，再删除全部
   私有凭据、数据和标记。失败路径的清理也必须有界，并校验进程身份。

C++ 程序使用公开 `mysql_module` 协程，全程保留原具名连接池，不重新配置。
恢复请求是调用方显式提交的新只读查询，不会自动重放写入。本验收验证重新连接行为，
不验证崩溃持久性、事务恢复或高可用。
