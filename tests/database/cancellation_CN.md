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

这些是手动启用的取消场景，与正常排空模式及超时／错误 fixture 分开。结果不覆盖 Redis TLS、事务取消、全部错误竞争、外部服务互通、跨平台验收、热更新或容量。实际执行的精确源码／运行时身份与清理证据由本地交接记录提供；测试说明本身不代表执行通过。
