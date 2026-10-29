# 数据库服务验收记录

[验收索引](../guides/database-validation_CN.md)

本页分别记录公开模块的真实服务正常／故障检查、插件 DSO 正常流程、MySQL DSO 写入事务、经服务端观察确认的请求取消、已发送 MySQL 事务查询取消、自有服务终止，以及可信 Redis TLS。每类检查保留各自的测试版本与环境；通过结果仅适用于对应范围。

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

## MySQL DSO 写入与两连接提交可见性

**2026-10-29**，`ark_database_public_plugin_service_tests` 的 `mysql-transactions` 模式通过，退出码为 `0`。实际执行的 ark 源码为 `4452acaeed6a3834d048f025f2970825803402bb`；本记录在执行后补入，程序源码未再变化。本地 arknet 源码与 CI 固定提交 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` 完全一致。机器为 Mac16,11／Apple M4 Pro／14 核／48 GiB，系统为 macOS 27.0.1（26A434）arm64。C++20 Debug 构建使用 Apple Clang 21.0.0、Boost.Asio 1.90.0 与 OpenSSL 3.6.4；私有服务为 MySQL Community Server 8.4.13，要求 TLS 并校验可信 CA 与 IP 身份。

C++ 宿主加载真实隐藏符号 MySQL 插件 DSO，仅使用公开协程 API，在专用 schema 中创建唯一 InnoDB 表。参数化 autocommit 和事务插入保留含 NUL、引号及反斜杠的二进制值，并确认独立的语句关闭诊断成功。两个公开连接配置对应不同的服务端连接 ID，观察连接处于 autocommit 模式。两个事务均可读取自身写入，观察连接在事务结束前看不到对应行；确认 commit 后，观察连接能读取已提交的二进制行，确认显式 rollback 后则看不到已回滚行。保留的查询与事务结果在后续操作、删除测试表和模块／管理器／运行时销毁后仍可读取。每次恢复均位于所属线程／共享执行器，已受理流程通过自然排空和可重复 stop 完成。

服务使用全新私有数据目录、专用 schema 及要求 SSL 的账户，账户仅获该 schema 的 CREATE、DROP、SELECT 和 INSERT 权限。独立清理查询确认剩余测试表为 `0`。自有服务通过认证的 `mysqladmin shutdown` 正常退出，并以退出码 `0` 回收；独立检查确认精确 PID 和 loopback 监听均不存在。私有数据、凭据与 TLS 密钥已删除。缺少 host 和非 loopback host 对照均在连接前失败，退出码为 `1`。

按[写入模式说明](../../tests/database/README_CN.md#真实插件动态库与真实服务)准备私有服务与环境变量后，执行：

```sh
cmake --build database-build --target ark_database_public_plugin_service_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_service_tests mysql-transactions
```

本轮覆盖单节点的两连接提交可见性，不代表全部隔离级别、事务取消、崩溃持久性或重启恢复验收。服务正常关闭属于清理，不是故障恢复证据。本轮不新增数据库协议、连接池策略、重试或强制停机。`check-database` 仅编译该显式模式，不启动服务；仅编译成功不能复现本次结果。

## 真实插件请求取消

**2026-10-26**，`ark_database_public_plugin_cancellation_tests` 的两种模式均通过，退出码均为 `0`。宿主仅使用公开接口，加载真实的隐藏符号数据库插件 DSO。

| 环境 | 配置 |
| --- | --- |
| 机器 | Mac16,11 · Apple M4 Pro · 14 核 · 48 GiB |
| 系统 | macOS 27.0.1（26A434）· arm64 |
| 构建 | Apple Clang 21.0.0 · C++20 · Boost.Asio · Debug · 启用 SSL · OpenSSL 3.6.4 |
| 实际执行的 ark | `dba34762dd58348784e46a2518f7505e52063d55` |
| arknet | CI 固定版本 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| 服务 | Redis 7.4.2：TCP 与密码认证；MySQL Community Server 8.4.13：强制 TLS 与证书校验 |

独立连接池先在服务端观察到唯一标识的操作，再通过现有 `task_group::cancel(id)` 请求取消。定时器仅降低轮询频率，等待时间不能替代操作已在服务端运行的证据。

| 模式 | 取消前的服务端观察 | 原生结果与实际影响 |
| --- | --- | --- |
| `redis` | `CLIENT LIST` 确认具名流水线在 BLPOP 中阻塞，此前的 INCR 已使计数变为 `1` | 保留 `client_errc::cancelled`、`command_may_have_run=true`、无传输错误与整数部分回复 `1`。显式 PING 恢复成功；观察到的阻塞命令退出后，独占计数仍为 `1`。 |
| `mysql` | `PROCESSLIST` 确认带标记的 UPDATE 处于 `User sleep` | 保留 `connection_errc::cancelled`、`outcome_unknown=true` 与 `operation_aborted` 传输状态。显式新查询恢复成功；观察者确认该语句在所有状态下均已消失，并经过有限静默观察期后，读取最终计数，本次为 `1`。 |

取消值仍作为服务结果返回，不转换为协程异常。请求与观察根任务分别在所属线程／共享执行器完成一次。重复取消尚未退出的任务会被接收，取消已退出 ID 则失败。两个受控测试均在各自的观察与驱动时限内完成托管任务和原生资源的自然停机，重复 stop 成功；插件／模块／管理器／运行时销毁后，拥有型取消结果仍可读取。这些时限仅用于测试，不构成强制或抢占式生产期限：`manager.stop()` 仍为协作式调用，不能中断阻塞中的钩子。本次不增加生产超时或取消策略，停机不使用 `cancel_all` 或强制销毁协程帧。

最终计数仅描述本次观察到的操作及有限观察窗口。MySQL 用例在结果未知时允许 `0` 或 `1`，本次观察到 `1`。取消不证明回滚、全局恰好执行一次，也不覆盖全部重试竞争。

按[独立 C++ 取消验收指南](../../tests/database/cancellation_CN.md)准备独占服务、私有认证配置与数据库权限。这些模式会写入独占 Redis 键或私有 MySQL 计数表，不应对共享服务运行。完成启用 SSL 的数据库构建配置后，执行：

```sh
cmake --build database-build --target ark_database_public_plugin_cancellation_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_cancellation_tests redis
./database-build/tests/database/ark_database_public_plugin_cancellation_tests mysql
```

`check-database` 仅编译该程序，不运行真实服务模式。两个自有服务均通过认证命令正常关闭；精确进程已独立确认不存在，私有数据、凭据与 TLS 密钥已删除。服务端终止不是本次测试动作。

## MySQL 已发送事务操作取消

**2026-10-29**，`ark_database_public_plugin_cancellation_tests` 的
`mysql-transaction` 模式通过，退出码为 `0`。验收程序在实际执行版本、交付提交和
本组合目录中的内容逐字节一致；本次文档集成不代表另一次真实服务运行。

| 身份／环境 | 配置 |
| --- | --- |
| 实际执行的 ARK 程序源码 | `28edd1a88c5e7179668aa387a9158d3db32a15f0` |
| 交付提交，程序未变化 | `5ce07533495a2b2cbb6fc31f57131dab3952c15a` |
| 程序 Git blob | `d833ed13b68cc7c84e518ad094efa22381989fa0` |
| arknet | CI 固定版本 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`；本地源码与固定版本归档一致 |
| 构建 | macOS 27.0.1（26A434）arm64；Apple Clang 21.0.0；C++20 Debug；Boost.Asio 1.90.0；OpenSSL 3.6.4 |
| 私有服务 | MySQL Community Server 8.4.13；`127.0.0.1:29889`；认证、强制可信 TLS 和 IP 身份校验 |
| Schema 权限 | 仅限私有 schema 的 CREATE、DROP、SELECT、INSERT 和 UPDATE |
| 原事务／观察连接 ID | `13`／`14` |

公开协程宿主加载真实的隐藏符号 MySQL 插件 DSO。事务先完成一次 UPDATE，
并保留自身读取到的 `n=1`，独立观察连接同时读取到 `n=0`。观察者确认原连接及带标记
慢 UPDATE 处于 `User sleep` 后，才调用 `task_group::cancel(id)`。
重复取消待完成任务会被接收；任务退出后，取消其 ID 则被拒绝。

原结果保留原生 `connection_errc::cancelled`、`outcome_unknown=true` 和
`operation_aborted`。事务状态仍为 `unconfirmed`，`discard_requested=true`，
无回滚回复或回滚码，也无清理、取消或租约归还异常。通过原具名连接池显式提交的新查询
在另一连接上成功。观察者等待原会话整体消失，再确认额外 500 ms 内原会话及带标记 SQL
均未重现，然后读取到最终 `n=0`。对私有服务通用日志的独立查询确认：前置 UPDATE 和
带标记慢 UPDATE 各收到一次。这些结果仅描述有限窗口内观察到的服务端效果，
不会把 API 结果转换为已确认的回滚回复，也不证明持久化。本次未替换事务或自动重放写入。

请求和观察根任务均在所属线程／共享执行器上完成一次，没有协程异常。
自然退出和重复 stop 均成功。后续操作、删表及模块／管理器／运行时销毁后，
保留的前置写入读取结果仍为 `1`，取消回复仍保留原始诊断。独立查询确认无剩余测试表。
自有 MySQL 进程通过认证正常关闭，以退出码 `0` 回收；精确 PID、监听端口、私有数据、
凭据和 TLS 密钥均已清理。

原有 `mysql` 和 `redis` 模式也均通过，最终计数均为 `1`；Redis 使用另一经过认证的
7.4.2 实例，且无剩余测试键。错误密码、不可信 CA 对照均按预期失败；未启用 SSL 的构建
在真实服务操作前拒绝两个 MySQL 模式。针对性原生回归通过 23 个用例、1147 条断言，
并通过公开 MySQL DSO 生命周期检查。这些结果保留原有的本机 macOS Boost Debug 范围，
不代表托管 CI、其他平台、AddressSanitizer、UndefinedBehaviorSanitizer 或
ThreadSanitizer 验收。原始日志和源码／二进制哈希仅保留在本地。

按[私有服务与取消操作说明](../../tests/database/cancellation_CN.md#mysql-事务内操作取消)
运行既有程序的 `mysql-transaction` 模式。本记录只覆盖一次已发送事务查询的取消。
acquire、BEGIN、prepare／execute／statement close、commit 或 rollback 阶段的取消，
以及其他竞争、事务恢复、崩溃持久性、高可用和容量，均不在本记录范围内。
较早的独立查询取消与提交可见性记录保留各自所测版本和未覆盖范围。

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

## 真实插件 Redis TLS

**2026-10-26**，`ark_database_public_plugin_service_tests` 的三个 Redis TLS 模式各执行一次，均通过。实际执行的 ark 为 `6cf58973c0aad587b6eb88cede768a9283eb2147`，CI arknet 固定版本为 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`。机器为 Mac16,11／Apple M4 Pro／14 核／48 GiB，系统为 macOS 27.0.1（26A434）arm64，构建采用 Apple Clang 21.0.0、C++20 Debug、Boost.Asio 与 OpenSSL 3.6.4。隔离 Redis 7.4.2 运行时由已验证的官方源码归档单独构建，使用 `BUILD_TLS=yes` 及该 OpenSSL 工具链。

私有回环实例要求密码认证，禁用明文端口与持久化，使用全新 CA／服务证书，证书仅包含 `127.0.0.1` 的 IP SAN。客户端证书认证已关闭，本次不验证双向 TLS。执行 DSO 模式前，OpenSSL 对照分别确认可信证书链／IP、不相关 CA 拒绝，以及 `localhost` 主机名不匹配。

| 模式 | 观察结果 |
| --- | --- |
| `redis-tls` | 通过认证执行二进制 ECHO 命令，再验证二进制／空 ECHO 与 PING 流水线顺序；拥有型命令与流水线回复在后续操作及宿主销毁后仍可读取。独立服务计数增加三次 ECHO 与一次 PING。 |
| `redis-tls-untrusted-ca` | 保留原生 `client_errc::connection_failed`、OpenSSL 证书校验传输错误、`command_may_have_run=false` 及空回复。该观察窗口内的 PING／ECHO 计数不变。 |
| `redis-tls-wrong-host` | 使用可信 CA 与 `localhost` 访问同一仅含 IP 的服务证书，保留相同证书校验拒绝；该观察窗口内的 PING／ECHO 计数不变。 |

三个 C++ 模式的退出码均为 `0`。调用方修改 host／password／CA 后，配置副本仍生效。每次继续执行及仅完成一次的根任务均位于所属线程／共享执行器。已接收工作通过普通 `manager.stop()/update()` 自然排空，再完成原生服务退出和可重复的 stop。模块／管理器／运行时销毁及原生 DSO 关闭后，拥有型回复／错误仍可读取。受控测试时限不增加强制或抢占式生产停机期限。

独立 `INFO commandstats` 观察仅覆盖隔离窗口内的 PING／ECHO；客户端错误与这些计数均不证明真实服务未收到任何 RESP 命令。现有有界协议端点 TLS 回归另行统计接收的 RESP 命令。自有 Redis 已通过认证的 `SHUTDOWN NOSAVE` 正常关闭并以退出码 `0` 回收；精确进程已独立确认不存在，私有数据、凭据与密钥已删除。

按[独立 C++ Redis TLS 指南](../../tests/database/redis_tls_CN.md)准备启用 SSL 的构建、私有端点／CA／主机名对照及计数观察。`check-database` 编译现有目标，但真实服务模式仍需显式执行：

```sh
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls-untrusted-ca
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls-wrong-host
```

本记录不覆盖双向 TLS、证书轮换／撤销，以及 TLS 请求取消、服务故障与恢复。较早的认证明文 TCP 和其他数据库记录保留原有源码标识及验收范围。

## 验收边界

DSO 流程覆盖正常插件销毁与原生关闭调用。`FreeLibrary` 或 `dlclose` 成功不证明操作系统已物理移除库映像。本机请求取消、服务端终止及 MySQL 写入／提交可见性验收仅覆盖记录中的操作。崩溃持久性、完整隔离级别覆盖、事务恢复、acquire／BEGIN／prepare／execute／statement close／commit／rollback 阶段的取消、双向 TLS 与证书轮换／撤销、Redis TLS 取消／故障恢复、其他取消／故障竞争、操作系统库映像移除、其他平台／部署环境与跨机器容量仍未验收。这些记录均不证明任意热更新安全或生产容量。

[显式重启恢复](../guides/database-restart_CN.md)和[两种服务端默认隔离级别](../guides/database-isolation_CN.md)分别有本地验收记录；其所测版本和范围不扩展上述较早用例的结论。
