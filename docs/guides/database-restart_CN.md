# 通过公开插件验证 Redis 重启恢复

公开插件测试程序验证自有 Redis 服务在同一端点重启后，由调用方显式提交的新只读请求。
程序加载真实插件 DSO，全程保留原具名连接池。构建命令、私有服务要求及故障协调步骤见
[重启操作手册](../../tests/database/restart_CN.md)。

## 恢复行为与结果

`configure()` 拒绝重复名称，不替换连接池。固定版本的 arknet 连接池会丢弃断开的租约，
后续 acquire 可以创建连接。以下恢复均由调用方显式提交新命令，不重放已中断的操作，
也不新增业务重试策略。

| 阶段 | 已验证结果 |
| --- | --- |
| 故障注入脚本中断服务端已观察到的只读请求 | `disconnected`、原生传输错误、`command_may_have_run=true` |
| 服务停止时提交新的 PING | `connection_failed`、原生传输错误、`command_may_have_run=false`（已知未发送） |
| 同端点的替换服务通过认证就绪验证 | 通过原池提交的新 PING 和二进制 ECHO 成功 |
| 自然关闭 | 根任务在所有者 executor 上恰好完成一次、无待处理任务、重复 stop 成功、结果仍有效 |
| 清理 | 两个自有 PID 和监听端口均不存在，私有凭据、数据及标记已删除 |

## 本地验收记录

2026-10-29，使用本机 IPv4 loopback 上的认证明文 TCP 通过验收。环境为
Mac mini、Apple M4 Pro（14 核）、48 GiB 内存，macOS 27.0.1 arm64，
AppleClang 21.0.0.21000334、CMake 4.3.3、Boost.Asio、OpenSSL 3.6.4 和 Redis 7.4.2。

| 源码 | 标识 |
| --- | --- |
| ARK 基线 | `7815493a2cc8c0413372f99277481b653f871133` |
| arknet 固定版本 | `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| 测试程序 Git blob | `c58095a676af7896685fc3dc29e41caca1b56865` |

本地 arknet 源码与固定版本参考副本一致。原始日志及源码/二进制散列仅保留在本地。
本次 TCP 重启仅覆盖只读恢复，不涵盖 TLS、MySQL、写入结果、持久化、生产高可用或容量。
本地服务执行与托管 CI、部署环境验收分别记录。
