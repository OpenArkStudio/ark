# 通过公开插件验证数据库重启恢复

公开插件测试程序验证自有 Redis 或 MySQL 服务在同一端点重启后，由调用方显式提交的
新只读请求。程序加载真实插件 DSO，全程保留原具名连接池。构建命令、私有服务要求
及故障协调步骤见[重启操作手册](../../tests/database/restart_CN.md)。

## 恢复行为与结果

`configure()` 拒绝重复名称，不替换连接池。固定版本的 arknet 连接池会丢弃断开的租约，
后续 acquire 可以创建连接。以下恢复均由调用方显式提交新命令，不重放已中断的操作，
也不新增业务重试策略。

| 阶段 | 已验证结果 |
| --- | --- |
| Redis：故障注入脚本中断服务端已观察到的只读请求 | `disconnected`、原生传输错误、`command_may_have_run=true` |
| Redis：服务停止时提交新的 PING | `connection_failed`、原生传输错误、`command_may_have_run=false`（已知未发送） |
| Redis：同端点的替换服务通过认证就绪验证 | 通过原池提交的新 PING 和二进制 ECHO 成功 |
| MySQL：中断服务端已观察到的 `SELECT SLEEP` | 原生 `transport`、传输错误、`outcome_unknown=true`，无行或服务端错误 |
| MySQL：停服时新提交 `SELECT 1` | 原生 `transport` 和 `connection_refused`、`outcome_unknown=false`（已知未发送） |
| MySQL：同端点的替换服务通过可信 TLS 认证就绪验证 | 通过原池显式提交的新查询返回整数 42 和包含 NUL 的字符串 |
| 自然关闭 | 根任务在所有者 executor 上恰好完成一次、无待处理任务、重复 stop 成功、结果仍有效 |
| 清理 | 两个自有 PID 和监听端口均不存在，私有凭据、数据及标记已删除 |

## 本地验收记录

2026-10-29，本机 IPv4 loopback 验收通过：Redis 7.4.2 使用认证明文 TCP，
MySQL 8.4.13 使用强制的可信 TLS 和只读 SELECT 账号。环境为
Mac mini、Apple M4 Pro（14 核）、48 GiB 内存，macOS 27.0.1 arm64，
AppleClang 21.0.0.21000334、CMake 4.3.3、Boost.Asio 和 OpenSSL 3.6.4。
MySQL 独立观察连接确认带标记查询的 `User sleep` 状态后，才终止自有 PID。
两个替换服务均使用原私有数据目录、配置与端点；通过原池提交的新只读请求成功，
全程未重新配置连接池。

| 源码 | 标识 |
| --- | --- |
| ARK 基线 | `e77c2d8a1fe59d8519c447376c1f5d9a267935dd` |
| arknet 固定版本 | `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| 测试程序 Git blob | `0b31502822ad0e181828b42a50859c2b093aa227` |

本地 arknet 源码与固定版本的 921 个文件全部一致。原始日志及源码/二进制散列仅保留在
本地。这些单端点重启仅覆盖只读恢复，不涵盖 Redis TLS 重启、写入结果、事务恢复、
持久化、生产高可用或容量。本地服务执行与托管 CI、部署环境验收分别记录。
