# 数据库服务验收

本索引用于选择已有的数据库验收流程并查找结果。
[详细服务记录](../reports/database-services_CN.md)保留各场景的测试版本、日期、
环境、观察结果和限制。重启与隔离级别使用下表链接的独立记录。

## 已记录结果

这些结果来自本地功能验收，仅适用于各自记录的源码和场景。下表不将多项结果合并为更广泛的
验收结论。

| 能力 | 已记录结果 | 运行工具／说明 | 详细记录 |
| --- | --- | --- | --- |
| <a id="公开模块与真实服务"></a>公开模块：Redis 与 MySQL | 正常与故障模式通过：拥有型结果、所属线程恢复、认证失败、已观察到的超时及显式请求恢复 | [公开服务与故障程序](../../tests/database/README_CN.md#公开模块真实服务验收) | [服务记录](../reports/database-services_CN.md#公开模块与真实服务) |
| <a id="真实插件动态库与真实服务"></a>真实插件 DSO 正常流程 | 两种模式均通过，覆盖拥有型结果、自然排空和重复 stop | `ark_database_public_plugin_service_tests` (`redis`, `mysql`) | [正常 DSO 记录](../reports/database-services_CN.md#真实插件动态库与真实服务) |
| <a id="mysql-dso-写入与两连接提交可见性"></a>MySQL DSO 写入 | 二进制插入、自身写入读取、独立连接的提交／回滚可见性及保留结果均通过 | `ark_database_public_plugin_service_tests mysql-transactions` | [写入记录](../reports/database-services_CN.md#mysql-dso-写入与两连接提交可见性) |
| <a id="真实插件请求取消"></a>服务端观察确认的请求取消 | 两种模式均通过；保留原生取消结果及有限窗口内的计数观察 | [C++ 取消程序](../../tests/database/cancellation_CN.md) | [取消记录](../reports/database-services_CN.md#真实插件请求取消) |
| <a id="真实插件服务端终止"></a>自有服务终止 | 两种模式均通过：向精确 PID 注入故障后返回已发送／结果未知的原生错误；脚本防护对照也通过 | [C++ 程序与 shell 脚本](../../tests/database/server_termination_CN.md) | [终止记录](../reports/database-services_CN.md#真实插件服务端终止) |
| <a id="真实插件-redis-tls"></a>Redis TLS | 认证二进制操作通过；不相关 CA 和错误主机名对照拒绝连接 | [Redis TLS 模式](../../tests/database/redis_tls_CN.md) | [TLS 记录](../reports/database-services_CN.md#真实插件-redis-tls) |
| 原连接池上的显式重启恢复 | 自有服务在同一端点重启后，Redis 与 MySQL 的新只读请求均通过 | [C++ 重启程序与故障脚本](../../tests/database/restart_CN.md) | [重启记录](database-restart_CN.md) |
| MySQL 一致性读隔离 | REPEATABLE READ 保留事务快照；READ COMMITTED 在后续读取中看到已提交变更 | [C++ 隔离级别程序](../../tests/database/isolation_CN.md) | [隔离记录](database-isolation_CN.md) |

## 运行流程

<a id="复现真实-dso-正常流程"></a>

使用[数据库构建配置](../../tests/database/README_CN.md#构建)
及所选行的操作说明。真实服务模式由独立 C++ 程序执行；
MySQL 要求启用 SSL。故障脚本使用 shell 工具。

运行前准备独占服务、私有数据与凭据，
以及对应的 TLS 信任配置和权限。缺少配置属于失败，
不会跳过并报告成功。执行后按对应流程清理服务和私有文件。

`check-database` 编译可选服务程序，不运行真实服务模式。编译和本地端点回归不能复现
这些真实服务结果。机器与源码标识属于详细记录，不适用于当前仓库的每一次构建。

## 验收边界

记录仅描述单机已测操作，不代表托管 CI、
其他平台或部署环境验收，也不证明生产容量。
重启与隔离级别的结果不扩展较早服务场景的结论。

自然排空与原生 DSO 关闭不证明操作系统已物理移除库映像，也不证明任意热更新安全。
`manager.stop()` 仍为协作式调用，不能中断阻塞钩子；测试时限不增加强制生产停机期限。

崩溃持久性、持久化、完整隔离级别覆盖、事务取消、双向 TLS、证书轮换／撤销、
Redis TLS 取消与故障恢复，以及其他取消／故障竞争，均不在这些记录的验收范围内。
请求取消不证明回滚或全局恰好执行一次。
详细记录保留各次观察结果及更具体的限制。
