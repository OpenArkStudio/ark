# 数据库服务验收

本页分别记录公开模块的真实服务正常／故障检查，以及通过真实插件 DSO 执行的正常流程。每类检查保留各自的测试版本与环境；通过结果仅适用于对应范围。

## 公开模块与真实服务

**2026-10-21**，四组公开接口程序在隔离的本机真实服务上全部通过。

| 环境 | 配置 |
| --- | --- |
| 机器 | Mac mini · Apple M4 Pro · 14 核 · 48 GiB |
| 系统 | macOS 27.0.1（26A434） |
| 构建 | Apple Clang 21.0.0 · C++20 · Boost.Asio · Debug |
| ark | `61ad91ae59e93391e03d685fa01ddf02ba0ae98e` |
| arknet | v0.2.2 · `c5432ca176edf9f4c06db03713f03764184c5cea` |
| 服务 | Redis 7.4.2：TCP 与密码认证；MySQL 8.4.11：密码认证与 TLS 证书校验 |

| 程序模式 | 结果 | 覆盖范围 |
| --- | --- | --- |
| 公开接口 · Redis | 通过 | 二进制流水线、后续请求、输入与结果所有权、所属线程恢复、停服排空 |
| 公开接口 · MySQL | 通过 | 查询与预处理结果、事务提交／回滚、所有权、所属线程恢复、停服排空 |
| 故障场景 · Redis | 通过 | 错误密码、服务端可见的阻塞命令超时、后续显式请求恢复 |
| 故障场景 · MySQL | 通过 | 错误密码、不可信 CA、服务端可见的慢查询超时、后续显式请求恢复 |

两种正常流程还验证了请求未完成时的宿主调用栈异常展开。服务进程和数据均为本次测试单独创建，执行后已停止进程。复现方式见[公开接口服务验收](../../tests/database/README_CN.md#公开模块真实服务验收)和[故障场景](../../tests/database/README_CN.md#公开模块真实服务负面验收)的命令。这些结果覆盖单机功能场景；跨机器容量、请求取消、服务端终止及动态库卸载需要单独验收。

## 真实插件动态库与真实服务

**2026-10-26**，`ark_database_public_plugin_service_tests` 的两种正常模式均通过，退出码均为 `0`。

| 环境 | 配置 |
| --- | --- |
| 机器 | Mac16,11 · Apple M4 Pro · 14 核 · 48 GiB |
| 系统 | macOS 27.0.1（26A434）· arm64 |
| 构建 | Apple Clang 21.0.0 · C++20 · Boost.Asio · Debug · OpenSSL 3.6.4 |
| ark | `9923f33b230e052cacceb09bff21f80c95c5027f` |
| arknet | CI 固定版本 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| 服务 | Redis 7.4.2：TCP 与密码认证；MySQL Community Server 8.4.13：强制 TLS、可信 CA 与 IP 身份校验 |

宿主通过标准管理器加载真实的隐藏符号 Redis 或 MySQL 插件 DSO，仅使用公开模块、管理器与任务接口，不链接模块实现对象。

| 程序参数 | 结果 | 协议覆盖范围 |
| --- | --- | --- |
| `redis` | 通过 · 退出码 `0` | 有序的二进制／空 ECHO 回复、PING 与后续显式命令 |
| `mysql` | 通过 · 退出码 `0` | 有符号整数、文本与 NULL 查询字段，二进制预处理结果、独立的语句关闭诊断，以及只读事务查询后确认的 commit 和 rollback |

两种流程均在首次所属线程轮询前关闭顶层任务入口，此时已受理的根任务仍待完成。真实操作与后续等待随后在自然排空期间完成。每次协程恢复和完成回调均运行于所属线程与共享执行器；根任务仅完成一次，重复停机成功。数据库模块、管理器与运行时销毁后，拥有型结果仍有效。流程不以任务取消替代排空。

服务为独立的 loopback 实例，使用全新私有数据目录与认证配置。MySQL 使用专用数据库，以及要求 SSL 的只读 SELECT 账户。两个独占进程均成功停止；私有数据、认证文件与 TLS 私钥已删除。流程不写入 key 或表数据，也不验证事务持久性或隔离性。

### 复现真实 DSO 正常流程

按[真实插件服务说明](../../tests/database/README_CN.md#真实插件动态库与真实服务)准备隔离服务、`ARK_DATABASE_*` 端点与认证变量、MySQL 数据库及可信 CA。完成启用 SSL 的数据库构建配置后，执行：

```sh
cmake --build database-build --target ark_database_public_plugin_service_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_service_tests redis
./database-build/tests/database/ark_database_public_plugin_service_tests mysql
```

这些模式由独立 C++ 程序执行，不创建或停止服务。缺少配置、构建中没有所选协议时，程序返回失败，不会跳过并报告成功。`check-database` 仅构建该程序及其 DSO，不运行真实服务模式。

## 验收边界

DSO 流程覆盖正常插件销毁与原生关闭调用。`FreeLibrary` 或 `dlclose` 成功不证明操作系统已物理移除库映像。请求取消、服务端终止、操作系统库映像移除、其他部署环境与跨机器容量仍需单独验收。这两类检查均不证明任意热更新安全或生产容量。
