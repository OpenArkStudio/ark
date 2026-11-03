# 数据库服务验收

本索引用于选择已有流程并查找[详细服务记录](../reports/database-services_CN.md)。**2026-11-03** 的结果使用同一执行构建 `3948d881`，数据库／core／配置／构建输入与 `10e385b4` 等价。直接模块正常流程、服务终止、重启与隔离结果保留各自记录的修订。见[资源生命周期合同](../project/lifecycle-closeout_CN.md)。

## 已记录结果

以下为本地功能结果，仅适用于各自记录的构建与操作。

<a id="mysql-dso-写入与两连接提交可见性"></a>
<a id="mysql-已发送事务操作取消"></a>
<a id="mysql-预处理执行取消"></a>
<a id="管理器-stop-发起取消"></a>
<a id="管理器-stop-取消事务"></a>
<a id="redis-tls-管理器-stop-取消"></a>

| 能力 | 记录结果 | 运行工具／说明 | 详细记录 |
| --- | --- | --- | --- |
| <a id="真实插件动态库与真实服务"></a>公开插件正常流程 | 3 种模式 × 1,351 轮：300 秒内 4,053 次退出码 `0`；Redis 二进制命令／流水线及 MySQL 查询／预处理／提交／回滚可见性 | `ark_database_public_plugin_service_tests`：`redis`、`mysql`、`mysql-transactions` | [构建、指纹与结果](../reports/database-services_CN.md#公开数据库服务流程) |
| <a id="公开模块与真实服务"></a>公开模块故障对照 | 2 种模式各一次，退出码 `0`；真实模块工厂，认证拒绝、服务端确认的超时与显式恢复 | [故障程序](../../tests/database/README_CN.md#公开模块真实服务负面验收) | [模块与 DSO 边界](../reports/database-services_CN.md#公开数据库服务流程) |
| <a id="真实插件请求取消"></a>服务端确认后显式取消请求 | 4 种 DSO 模式各一次，退出码 `0`；Redis／MySQL 与事务／预处理执行保留原生未知结果及有限窗口观察 | [C++ 取消程序](../../tests/database/cancellation_CN.md) | [取消结果](../reports/database-services_CN.md#真实插件请求取消) |
| 管理器 stop 发起取消 | 5 种 DSO 模式各一次，退出码 `0`：Redis TCP／TLS、MySQL autocommit 与事务／预处理；独立观察者验证会话退出与计数效果 | [C++ manager-stop 模式](../../tests/database/cancellation_CN.md#管理器-stop-发起取消) | [流水线／autocommit](../reports/database-services_CN.md#管理器-stop-发起取消)、[事务／预处理](../reports/database-services_CN.md#管理器-stop-取消事务)、[Redis TLS](../reports/database-services_CN.md#redis-tls-管理器-stop-取消) |
| <a id="真实插件-redis-tls"></a>Redis TLS 对照 | 3 种 DSO 模式各一次，退出码 `0`；认证二进制操作成功，不相关 CA 与错误主机名返回原生证书拒绝 | [Redis TLS 模式](../../tests/database/redis_tls_CN.md) | [TLS 观察](../reports/database-services_CN.md#真实插件-redis-tls) |
| 直接调用公开模块的正常流程 | 2026-11-02 · `feff48ac`；拥有型输入／结果与自然排空保留此源码身份 | [公开服务程序](../../tests/database/README_CN.md#公开模块真实服务验收) | [独立源码与范围](../reports/database-services_CN.md#公开模块与真实服务) |
| <a id="真实插件服务端终止"></a>自有服务终止 | 2026-10-26 · `0722b8bb`；2 种模式在精确 PID 故障后返回已发送／未知的原生错误；脚本防护通过 | [C++ 程序与 shell 脚本](../../tests/database/server_termination_CN.md) | [终止记录](../reports/database-services_CN.md#真实插件服务端终止) |
| 原连接池显式重启恢复 | 2026-10-29 · 基线 `e77c2d8a`；自有服务在同端点重启后，Redis／MySQL 新只读请求恢复 | [C++ 重启程序与脚本](../../tests/database/restart_CN.md) | [重启记录](database-restart_CN.md) |
| MySQL 一致性读隔离 | 2026-10-29 · `093dfcb9`；REPEATABLE READ 保留快照，READ COMMITTED 在下次读取看到已提交变更 | [C++ 隔离级别程序](../../tests/database/isolation_CN.md) | [隔离记录](database-isolation_CN.md) |


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
重启与隔离级别的结果仅适用于各自源码和操作。

自然排空与原生 DSO 关闭不证明操作系统已物理移除库映像，也不证明任意热更新安全。
`manager.stop()` 仍为协作式调用，不能中断阻塞钩子；测试时限不增加强制生产停机期限。

崩溃持久性、持久化、完整隔离级别覆盖、事务恢复，以及 acquire／BEGIN／prepare／
statement close／commit／rollback 阶段的取消及其他 execute 竞争，均未在这些记录中验收。双向 TLS、
证书轮换／撤销、其他 Redis TLS 取消阶段与故障恢复，以及其他取消／故障竞争也未覆盖。
请求取消不证明回滚或全局恰好执行一次。
即使观察到最终零值，已发送事务查询的取消结果仍为未确认。
详细记录保留各次观察结果及更具体的限制。
