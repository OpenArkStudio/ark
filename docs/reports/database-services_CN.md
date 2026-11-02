# 数据库服务验收记录

[验收索引](../guides/database-validation_CN.md)

结果仅适用于下方记录的源码、二进制身份与操作。[资源生命周期合同](../project/lifecycle-closeout_CN.md)区分业务完成与进程资源清理；其他场景保留各自测试修订。

## 停机候选版本资源清理

**2026-11-02**，在新建、独占的单节点 MySQL 与 Redis 服务上，**12 次原生程序执行全部通过**。正常业务流程在 `running` 时完成，随后宿主停止资源。取消场景在服务端确认请求已执行后，由所属线程显式取消任务，等待原生结果与观察任务交付，再停止宿主；不据此验收真实服务请求尚未结束时，由进程 stop 发起的取消。

| 身份 | 实际执行配置 |
| --- | --- |
| 候选源码 | 执行时工作区干净，`bc90b68199c7a8d5ccb23e70785454870e160f3e` |
| 复用的数据库构建 | `7767bbe0274a1b3e013f114546f567c079d03c00`。`tests/database`、`src/core`、`src/plugins/mysql`、`src/plugins/redis` 至候选版本均无变化；这是源码等价验证，不表示产物在候选提交上构建。二进制 SHA256 在执行前后相同 |
| 机器／系统 | Mac16,11 · Apple M4 Pro · 14 核 · 48 GB · macOS 27.0.1（26A434）· arm64 |
| 客户端 | Apple Clang 21.0.0 · C++20 Debug · 固定 Boost 1.92.0 头文件 · OpenSSL 4.1.0 · 启用 SSL 的 Boost.Asio |
| arknet | 配置源码 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| 服务端 | MySQL Community 8.4.13 arm64，启用认证与 TLS 证书校验；Redis 7.4.2 使用密码认证 TCP；Redis TLS 源码为 `a0a6f23d997b024689ba157916837f493a593a34`，服务端 OpenSSL 3.5.9 |

<a id="真实插件动态库与真实服务"></a>
<a id="mysql-dso-写入与两连接提交可见性"></a>
<a id="复现真实-dso-正常流程"></a>

### 正常 DSO 流程与公开接口故障对照

| 原生入口与模式 | 结果／观察 |
| --- | --- |
| `ark_database_public_plugin_service_tests`：`redis`、`mysql`、`mysql-transactions` | 3 次通过。加载真实隐藏符号插件 DSO，验证所属线程／共享执行器交付、仅一次完成、析构后保留的拥有型结果及可重复 stop。MySQL 覆盖二进制预处理插入、autocommit、显式 commit／rollback 与独立连接可见性 |
| `ark_database_public_failure_service_tests`：`redis`、`mysql` | 2 次通过。使用真实公开模块实现，覆盖错误凭据、服务端可见的请求超时与显式恢复；MySQL 还拒绝不相关 CA |

<a id="真实插件请求取消"></a>
<a id="mysql-已发送事务操作取消"></a>
<a id="mysql-预处理执行取消"></a>

### 服务端观察确认后显式取消任务

`ark_database_public_plugin_cancellation_tests` 四种模式均通过。独立连接先确认带标记的操作正在执行，再重复调用 `task_group::cancel(id)`；等待时长不作为活动确认屏障。根任务在所属线程／共享执行器上各完成一次，已结束的 ID 拒绝取消，显式新请求恢复，保留结果在资源停机后仍有效。

| 模式 | 原生结果与有限窗口观察 |
| --- | --- |
| `redis` | 命名流水线在 INCR 后进入 BLPOP；保留 `client_errc::cancelled`、`command_may_have_run=true` 与部分整数回复 `1`。阻塞结束后，计数仍为 `1` |
| `mysql` | 带标记的 UPDATE 处于 `User sleep`；保留 `connection_errc::cancelled`、`outcome_unknown=true` 与 `operation_aborted`。操作消失后，最终计数为 `1` |
| `mysql-transaction` | 未提交预写入后，精确事务会话进入慢查询；结果未知／未确认、已请求丢弃且无 rollback 回复。原会话消失，恢复使用新连接，有界静默窗口内未出现重放，最终计数为 `0` |
| `mysql-statement` | 精确事务会话进入参数化 Execute；保留同样的未知／未确认／请求丢弃结果，无 statement-close 或 rollback 回复。原会话消失，新连接恢复与有界无重放检查通过，最终计数为 `0` |

观察到零不代表 API 已确认 rollback；有限窗口观察不证明持久性或全局仅执行一次。此处覆盖取消、原生完成与随后可重复的资源 stop，不覆盖进程 stop 对待完成真实服务请求发起取消。

<a id="真实插件-redis-tls"></a>

### Redis TLS

`ark_database_public_plugin_service_tests` 三种 TLS 模式均在私有、密码保护、仅开放 TLS 的服务上通过。新证书只含 `127.0.0.1` IP SAN；OpenSSL 对照确认可信证书链／IP，以及 `localhost` 主机名不匹配。

| 模式 | 观察结果 |
| --- | --- |
| `redis-tls` | 二进制 ECHO 与有序二进制／空 ECHO、PING 流水线成功；拥有型输入／配置与结果在后续操作、宿主析构后仍有效 |
| `redis-tls-untrusted-ca` | 原生 `client_errc::connection_failed`、证书校验传输错误、`command_may_have_run=false`，无回复 |
| `redis-tls-wrong-host` | 可信 CA 配合 `localhost` 访问仅含 IP 的证书，返回同样的校验拒绝 |

两个负面对照期间，PING／ECHO 计数未增加。这不证明没有任何 RESP 命令到达服务端；接收命令计数属于独立的协议端点证据。未执行双向 TLS、证书轮换／撤销及 TLS 取消／恢复。

<Collapsible>
<CollapsibleTrigger>执行的二进制指纹</CollapsibleTrigger>
<CollapsibleContent>

| 执行的二进制 | SHA256 |
| --- | --- |
| `ark_database_public_plugin_service_tests` | `e763e25372866b45461185e51bddf6734b80022e8724335ee3f06b46ec89b531` |
| `ark_database_public_plugin_cancellation_tests` | `4effcf1c105bb5e66f124281849f2352dfcd96fd746fcac5e141cdf1e610d2da` |
| `ark_database_public_failure_service_tests` | `eb39463bc477424ca19487a8fd572c8e4909bfa8ea5503f7445973051e3a65ad` |
| `ark_database_mysql_plugin_d.dylib` | `48e8adc1f1b2e83e31ef496728b6f312f26c3fc5910e0faf368cbcabb4177293` |
| `ark_database_redis_plugin_d.dylib` | `e6bf0a2b9476d79d5b846643dfb59511418f3e6086866277a5990caa2938c13a` |

</CollapsibleContent>
</Collapsible>

每个原生子进程设置 45 秒观察期限，以及随后最多 5 秒的自有子进程终止期限，均未触及；这些是测试控制，不是生产停机默认值。服务、端口、数据、凭据和证书均由本批次独占。结束后 schema 与 Redis 数据库为空，自有进程已停止并回收，私有数据目录、密钥与凭据配置已删除。原始回执、日志与服务端二进制指纹仅保存在本地。

复现沿用[正常 DSO 模式](../../tests/database/README_CN.md#真实插件动态库与真实服务)、[故障对照](../../tests/database/README_CN.md#公开模块真实服务负面验收)、[取消程序](../../tests/database/cancellation_CN.md)和 [Redis TLS 对照](../../tests/database/redis_tls_CN.md)。真实服务执行保持显式选择；`check-database` 仅编译不复现服务验收。

## 其他已记录范围

以下操作未在停机候选版本上执行，仍适用于各自行列出的修订；arknet 均为 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`。

<a id="公开模块与真实服务"></a>

| 范围／源码 | 保留结果与限制 |
| --- | --- |
| 直接调用公开模块的正常流程 · 2026-11-02 · `feff48ac49ed11f5166533a9e5e74a02ce4567ef` | Redis／MySQL 输入与结果所有权、二进制流水线或查询／预处理／事务结果、所属线程交付，以及工作未完成时的宿主异常展开通过。使用同一 Mac16,11／macOS 27.0.1、Clang 21、C++20 Debug、Boost 1.92.0／OpenSSL 4.1.0 与私有 Redis 7.4.2／MySQL 8.4.13 环境。它们采用自然排空，不属于候选版本 DSO 或 stop 取消证据。[原生操作说明](../../tests/database/README_CN.md#公开模块真实服务验收) |
| 原连接池显式重启 · 2026-10-29 · ARK 基线 `e77c2d8a1fe59d8519c447376c1f5d9a267935dd` | 被中断的只读请求保留未知结果；自有 Redis／MySQL 在同端点替换后，新请求通过原池恢复，不重配池。程序 blob 为 `0b31502822ad0e181828b42a50859c2b093aa227`；不证明写入重放、事务恢复或高可用。[完整环境与记录](../guides/database-restart_CN.md) |
| MySQL 隔离级别 · 2026-10-29 · `093dfcb9a430beedcd86de19eb0d70c8aae9160e` | REPEATABLE READ 保留快照；READ COMMITTED 在下次读看到已提交变更。相反级别与非法配置对照通过，不覆盖锁定读或 SERIALIZABLE。[完整环境与记录](../guides/database-isolation_CN.md) |

<Collapsible>
<CollapsibleTrigger>其他修订的配置与协议对照</CollapsibleTrigger>
<CollapsibleContent>

2026-10-29 的对照使用 macOS C++20 Debug、Boost 1.90.0 和 OpenSSL 3.6.4，不属于候选版本执行。

| 源码 | 已记录对照 |
| --- | --- |
| `4452acaeed6a3834d048f025f2970825803402bb` | MySQL 写入模式缺少 host 或使用非回环 host 时，在连接前以 `1` 拒绝 |
| `28edd1a88c5e7179668aa387a9158d3db32a15f0` | 事务取消 general log 中，一次预写入与一次带标记慢 UPDATE；程序在 `5ce07533495a2b2cbb6fc31f57131dab3952c15a` 未变。关闭 SSL 的拒绝对照属于该记录 |
| `30f963cade5162d8b3b474646dc8c123b987a5be` | 预处理取消 general log 中，一次 Prepare、一次 Execute 与一次预写入；七个配置／构建模式拒绝属于该记录 |
| `7fde1fbe7f6607c275987ba75bcccf8630853f21` · 2026-11-02 · Boost 1.92.0／OpenSSL 4.1.0 | `ark_database_queue_tests`：18 个用例／131 条断言；筛选后的排队 acquire 协议端点用例：1 个用例／146 条断言，4 个用例被排除。不代表真实服务等待连接竞争，也不扩大取消阶段验收范围 |

</CollapsibleContent>
</Collapsible>

## 真实插件服务端终止

**2026-10-26**，`ark_database_public_plugin_server_termination_tests` 的两种模式均通过。C++ 宿主与独立故障脚本的退出码均为 `0`；两个自有服务的预期 `SIGKILL` 退出状态均为 `137`。

| 环境 | 配置 |
| --- | --- |
| 机器 | Mac16,11 · Apple M4 Pro · 14 核 · 48 GiB |
| 系统 | macOS 27.0.1（26A434）· arm64 |
| 构建 | Apple Clang 21.0.0 · C++20 · Boost.Asio · Debug · 启用 SSL · OpenSSL 3.6.4 |
| 实际执行的 ark | `0722b8bdd2e2c240657a3ceea63428cbbff0f821` |
| arknet | CI 固定版本 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| 服务 | Redis 7.4.2：TCP 与密码认证；MySQL Community Server 8.4.13：强制 TLS 与证书校验 |

宿主使用公开接口和真实隐藏符号插件 DSO。独立服务连接必须先观察到精确匹配的活动请求，才释放私有故障屏障；轮询间隔不能证明请求正在执行。

| 模式 | 故障前观察到的只读请求 | 服务终止后的原生结果 |
| --- | --- | --- |
| `redis` | `CLIENT LIST` 确认名称完全匹配的客户端正在 `XREAD` 中阻塞 | 保留 `client_errc::disconnected`、`command_may_have_run=true` 与 EOF 传输状态 |
| `mysql` | `PROCESSLIST` 确认带标记的 `SELECT SLEEP(60)` 连接处于 `User sleep` | 保留 `connection_errc::transport`、`outcome_unknown=true` 与 TLS stream truncation |

独立 shell 脚本在向单个自有 PID 发送信号前，先按物理路径解析私有文件，拒绝路径逃逸或符号链接协调文件；随后重新核对归属、PID／启动时间、可执行文件、私有数据工作目录、PID 文件及该 PID 的回环监听。MySQL 还核对私有 defaults-file 参数。这些防护仅用于按测试要求准备的一次性实例，不面向共享服务或已安装服务。

两个模式均拒绝将任务取消、操作超时、SQL 服务错误或协程异常作为通过结果。请求和观察根任务在所属线程／共享执行器上各完成一次，并在协作式 stop 前退出。受控测试在各自的观察与驱动时限内完成自然停机；重复 stop 成功，模块／管理器／运行时销毁及原生 DSO 关闭后，拥有型结果／错误仍可读取。`manager.stop()` 仍不能中断阻塞钩子，本次不增加强制或抢占式生产期限。原生传输错误细节由平台决定。

父进程已回收自有服务、故障脚本与宿主子进程，独立确认精确进程不存在，并删除私有数据、凭据与 TLS 密钥。本次未验证服务重启恢复、持久化或写入效果。

按[独立 C++ 服务终止指南](../../tests/database/server_termination_CN.md)准备私有服务、故障脚本协调与端点变量。故障脚本需要 `lsof`、`ps` 和 `rg`。完成启用 SSL 的数据库构建配置后，先按指南准备对应的自有故障脚本，再运行所选模式：

```sh
cmake --build database-build --target ark_database_public_plugin_server_termination_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_server_termination_tests redis
./database-build/tests/database/ark_database_public_plugin_server_termination_tests mysql
```

独立脚本防护已通过 20 个无效路径／标记用例及一个有效路径拦截对照。下述独立入口不启动服务，也不检查真实进程或向其发送信号：

```sh
bash tests/database/owned_server_fault_actor_guards.sh
```

`check-database` 仅编译该可选程序，不执行服务模式，也不向服务发送信号。仅编译通过的 CI 不代表另一次真实服务运行。

## 验收边界

DSO 流程覆盖正常插件销毁与原生关闭；`dlclose`／`FreeLibrary` 成功不证明操作系统已物理移除库映像。生产容量、任意回调生命周期与热更新安全均未验收。当前记录不覆盖真实服务请求待完成时由 stop 发起取消、真实服务等待连接竞争、其他取消阶段与竞争、崩溃持久性、完整隔离级别、事务恢复、双向 TLS、证书轮换／撤销或 Redis TLS 故障恢复。AddressSanitizer、UndefinedBehaviorSanitizer、ThreadSanitizer、其他平台与托管 CI 均为独立范围。
