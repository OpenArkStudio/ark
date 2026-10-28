# 数据库接入测试

[English](README.md)

这些开发目标覆盖原生服务、公开协程模块及实际插件的注册与停机。
`ARK_BUILD_DATABASE_TESTS` 默认关闭，启用测试不会同时开启应用运行时插件选项。
应用用法见公开 [MySQL](../../docs/reference/mysql_CN.md) 与
[Redis](../../docs/reference/redis_CN.md) 接口。

公开模块测试通过插件私有工厂创建拥有型接口对象，具体实现类仅定义在实现 `.cpp` 中，
保留直接生命周期和协议端点覆盖。独立的 `ark_database_public_plugin_service_tests`
仅使用公开 API target 并加载实际的隐藏符号 DSO，不链接私有工厂或实现源文件。
关闭运行时插件时仍可通过 `ark::mysql`、`ark::redis` 使用公开头文件，
见[消费方检查](../consumer/README.md)。

## 构建

按[网络构建说明](../../docs/networking_CN.md)初始化既有依赖。
显式指定包含原生 Redis、MySQL 客户端和 `arknet/base/manual_runtime.hpp`
的本地 arknet 仓库；开发测试不会修改生产依赖版本，
也不会下载功能分支。

```sh
cmake -S . -B database-build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  "-DVCPKG_MANIFEST_FEATURES=boost;database" \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_TESTS=OFF -DARK_BUILD_CORE_TESTS=OFF \
  -DARK_BUILD_DATABASE_TESTS=ON -DARKNET_SOURCE_DIR=/absolute/path/to/arknet \
  -DASIO_STANDALONE=OFF -DARKNET_ENABLE_SSL=ON
cmake --build database-build --target ark_database_queue_tests \
  ark_database_task_tests ark_database_task_contract_tests \
  ark_database_redis_stop_tests ark_database_redis_tls_tests \
  ark_database_redis_tests ark_database_mysql_tests \
  ark_database_service_awaitable_tests --parallel 2
```

使用 Standalone Asio 时设为 `ASIO_STANDALONE=ON`、
`VCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON` 和 `VCPKG_MANIFEST_FEATURES=database`，
必要时指定 `ARKNET_ASIO_INCLUDE_DIR`。仅在 `ARKNET_ENABLE_SSL=ON` 时构建 MySQL 测试；
即使显式使用明文 MySQL 连接，认证仍需要 OpenSSL。Redis 明文连接测试不要求此选项。
队列测试与 core/network 一样复用既有 `tests/network/doctest.h`，
真实服务检查是独立 C++ 可执行程序，不下载新测试框架。

## 运行本地回归

```sh
cmake --build database-build --target check-database
# 队列、停止和协程回归；也可以直接运行可执行文件。
bash scripts/test_databases.sh database-build local
./database-build/tests/database/ark_database_queue_tests --list-test-cases
```

队列用例覆盖操作数和拥有输入的配额、已完成未派发的操作、并发完成生产者、拥有线程派发、
回调异常、重入，以及停止时释放捕获。Redis 停止回归验证捕获对象析构时销毁服务的场景；
不需要运行中的数据库，仅使用 IPv4 回环端口 1。
状态回归还通过明确构造的输入验证获取失败的错误与回复映射，不代表复现了原生连接池故障。

`check-database` 不运行真实服务测试。缺少端点配置会明确报错，不会跳过后当作成功。

## 私有服务的连接池退出

`ark_database_service_retirement_tests` 不需要数据库服务。启用 SSL 时覆盖拥有连接池的
MySQL、明文 Redis 和 TLS Redis 服务；关闭 SSL 时覆盖明文 Redis。测试通过真实连接池的
执行线程交付被拒绝的获取请求，并验证显式关闭后，该请求的回调、捕获对象和拥有的工作线程
均已结束。空主机名校验用于验证同一协程中两个已受理请求的连续执行，不建立网络连接。
这些用例不代表数据库握手、活动子连接或部署容量验收。

DRAIN 首先停止顶层任务准入，保留服务和协程桥的准入，使已受理流程能继续发起后续请求，
并持续驱动结果交付。所有所属流程、协程桥和事务参与方排空，且最外层协程与 update 调用栈
返回后，再调用服务的 `stop_accepting()`。此时 `logically_drained()` 同时检查准入关闭、
独立共享的活动计数和完成队列排空状态；它不能检查外部执行器，也不能替代调用方的外层执行检查。

`retire_native()` 调用服务所拥有连接池的永久 `close_and_wait()` 屏障，仅成功后才将
`native_retired()` 设为真。返回 false 表示尚未完成，并保留原生 `last_error`；异常原样传播，
连接池所有者仍保留，包括原生连接池已经记录的关闭失败。不承诺自动重试、取消或固定的退出时限。
原有 `stop()` 仍抑制回调并请求异步关闭。事务服务借用连接池，不增加此拥有型服务屏障。
公开插件使用这些方法实现宿主驱动的退役阶段；管理器先排空任务并检查模块准备状态。

启用 SSL 时，本地回归还包含私有事务状态测试。独立的 [MySQL 事务测试](TRANSACTIONS_CN.md)
需要真实临时服务，验证多条语句及最终提交/回滚始终使用同一个原生 lease。

## Redis TLS 回归

启用 `ARKNET_ENABLE_SSL=ON` 时，`check-database` 也会运行原生 TLS 回归。
还可以单独构建并运行：

```sh
cmake --build database-build --target check-database-redis-tls
# 或：./database-build/tests/database/ark_database_redis_tls_tests
```

私有 `redis_service` 别名保留原有明文连接 API。私有 `tls_redis_service` 使用 arknet TLS
连接池，复用相同的请求、取消和拥有线程派发实现。它验证证书链及配置的连接主机；不受信任的
CA 或错误主机名必须在测试端点收到任何 RESP 命令（包括 `HELLO`）前失败。成功的二进制
流水线请求在服务销毁后仍保留拥有型结果。已发送请求的取消结果通过 `update()` 和模块协程
执行器交付，并保留 `command_may_have_run`。

可执行程序绑定随机 IPv4 回环端口，不需要启动 Redis 服务。带超时的 RESP-over-TLS 协议
测试端点只验证上述约定，不代表真实 Redis 兼容性或容量验收。测试直接读取
`ARKNET_SOURCE_DIR/tests/certs` 中公开的测试专用证书与密钥（arknet，BSL-1.0；见该目录
所属仓库的 `LICENSE` 和证书 README），不会复制到应用目录，也不得用于部署。
`ARKNET_ENABLE_SSL=OFF` 不构建此目标，明文服务仍可使用。

## Redis 服务检查

准备隔离的 Redis 兼容服务。测试创建唯一的 `ark-private-service-...` key，验证二进制
SET/GET 后删除该 key，并检查结果所有权、模块线程派发、配额恢复、停止后的回调抑制，
以及从业务回调中销毁服务。

例如，在另一终端启动临时本地服务：

```sh
redis-server --bind 127.0.0.1 --port 26791 --protected-mode yes \
  --save '' --appendonly no
```

然后运行：

```sh
export ARK_DATABASE_REDIS_HOST=127.0.0.1
export ARK_DATABASE_REDIS_PORT=26791
# 按需设置 ARK_DATABASE_REDIS_USER / ARK_DATABASE_REDIS_PASSWORD。
bash scripts/test_databases.sh database-build redis
# 或：./database-build/tests/database/ark_database_redis_tests
```

测试结束后停止临时服务。运行脚本不会启动或停止服务、安装工具，也不会为真实服务测试选择端点。
有写入的测试不要连接业务数据。MySQL 的配置与测试内容见 [MYSQL_CN.md](MYSQL_CN.md)。
`all` 会依次运行本地、Redis 和已构建的 MySQL 程序，需要配置两种真实服务。

## 内部约定与生命周期

网络 IO 由库拥有。原生回调只把拥有型结果移动到模块预留的完成记录，业务回调由 `update()`
在构造服务的线程上派发。提交、更新、停止与销毁服务均需在该线程执行。等池期间也计入输入容器和
字符串容量；业务回调执行及其捕获释放后才归还额度。原生结果容量限制继续生效，任意用户回调捕获
不属于输入字节额度。

既有 `submit*()` 返回 false 表示未受理且无回调。新增私有 `start*()` 返回逻辑请求 ID，
拒绝时即使返回 ID 0 也会回调终态。`cancel(id)` 请求取消；`stop_accepting()` 停止准入，
继续交付已受理操作的终态。已受理操作只完成一次，除非 `stop()` 移除尚未开始的业务回调。
同一个经溢出检查的截止时间覆盖等池和执行。保留原生服务端、传输错误、拥有型结果及写入结果不确定
状态，不重放写操作。公开模块通过协程接口返回原生拥有型结果。
[协程桥测试](COROUTINES_CN.md)说明所属执行器、有界交付、逻辑取消与显式关闭排空约定。

`stop()` 抑制业务回调并请求关闭连接池，**不等待**原生回调、传输对象、TLS 资源和 IO 线程销毁。
捕获析构重入的修复通过局部变量持有队列，先请求原生池关闭再释放捕获，不新增 IO 排空屏障。
运行时插件使用这些辅助服务，并在独立的原生退役阶段等待资源退出。
这些检查不代表操作系统动态库卸载或热更新验收。

## 公开插件检查

`ark_database_public_redis_tests` 和 `ark_database_public_mysql_tests` 使用本机协议端点
验证拥有型结果、取消、TLS 与正常停机。`ark_database_public_redis_plugin_tests` 和
`ark_database_public_mysql_plugin_tests` 加载实际隐藏符号动态库。
MySQL 检查要求开启 SSL，真实服务目标仍需显式配置端点。

公开 MySQL 协议端点还通过事务占用唯一连接，并确认单个等待名额已满，再取消等待中的
查询。该检查验证确定未发送的取消结果、所属线程交付、原事务仍可用，以及管理器排空
期间同一 socket 上的查询和预处理操作。这是本机协议回归，不代表真实服务取消验收。

## 公开模块真实服务验收

`ark_database_public_service_tests` 通过公开的 `mysql_module`、`redis_module`
接口调用真实实现，使用已注册消费模块的 `task_group` 和管理器共享的
`manual_runtime`。这是独立的 C++ 可执行程序；`check-database` 仅编译此程序，不运行，也不连接服务。

```sh
cmake --build database-build --target ark_database_public_service_tests --parallel 2
# 先配置隔离服务的端点，再选择一种协议或同时运行两种协议。
bash scripts/test_databases.sh database-build public-redis
bash scripts/test_databases.sh database-build public-mysql
bash scripts/test_databases.sh database-build public-services
# 也可以直接运行：
./database-build/tests/database/ark_database_public_service_tests redis
./database-build/tests/database/ark_database_public_service_tests mysql
```

Redis 要求设置 `ARK_DATABASE_REDIS_HOST` 和 `ARK_DATABASE_REDIS_PORT`。
可选的 `ARK_DATABASE_REDIS_USER`、`ARK_DATABASE_REDIS_PASSWORD` 用于认证；
验证认证路径时，需使用启用了认证的临时服务。测试在注册后修改调用方配置，
验证有序的二进制 ECHO/ECHO/PING 流水线及后续命令，不执行写入。

MySQL 要求设置 `ARK_DATABASE_MYSQL_HOST`、`ARK_DATABASE_MYSQL_PORT`、
`ARK_DATABASE_MYSQL_USER`、`ARK_DATABASE_MYSQL_PASSWORD`（必须存在，可以为空）、
`ARK_DATABASE_MYSQL_DATABASE` 和 `ARK_DATABASE_MYSQL_CA`。连接必须使用 TLS 并验证证书。
使用临时数据库，以及具备 CREATE、DROP、SELECT、INSERT 权限的账号。
测试创建唯一的 `ark_public_service_...` InnoDB 表，执行文本查询、二进制预处理查询、
事务查询和预处理插入，再验证提交数据可见、回滚数据不可见。流程结束后删除测试表；
断言失败且连接仍可用时，也会尝试删除。连接失败、活动事务清理或强制退出进程可能留下测试表，
需随临时数据库一起清理。

两种协议均检查协程在拥有线程及共享执行器上恢复、输入与配置的所有权，以及后续操作和
模块/运行时销毁后结果仍然有效。管理器在已受理任务等待期间进入自然排空状态，
允许流程完成后续请求，再验证成功且可重复调用的停止操作。每种协议还会在 IO 等待期间主动
触发宿主栈展开并重复整个流程：命名协程函数按值拥有输入，拥有者析构时排空流程，外层作用域
再验证完成结果与保留的数据。这些真实服务用例不注入服务故障，也不测试取消。协议端点回归和动态库回归仍是独立检查；此处结果不代表动态库卸载、
故障恢复或容量验收。

程序必须显式配置端点；配置缺失、无效或验收失败均返回非零状态，不打印凭据或任意原生错误文本，
也不会启动或停止服务。`all` 也运行这两种公开模块真实服务模式，因此要求启用 SSL，
并配置两种服务的端点。

## 真实插件动态库与真实服务

`ark_database_public_plugin_service_tests` 通过实际插件管理器加载现有隐藏符号 Redis 或
MySQL 动态库，只调用公开模块、管理器与任务接口。可执行程序不编译或链接模块实现对象。
`check-database` 仅构建该程序及其插件动态库，不运行真实服务流程。

```sh
cmake --build database-build --target ark_database_public_plugin_service_tests --parallel 2
./database-build/tests/database/ark_database_public_plugin_service_tests redis
./database-build/tests/database/ark_database_public_plugin_service_tests mysql
```

沿用上文的 `ARK_DATABASE_*` 端点与认证变量。MySQL 要求启用 SSL、设置数据库，
并配置可信的 TLS CA。Redis 检查有序的二进制和空 ECHO 回复、PING 与后续命令。
MySQL 检查有符号整数、文本、NULL 查询字段，二进制预处理结果与语句关闭，
以及事务查询后确认的 commit 和 rollback。流程不写入数据或创建表，不验证事务持久性或隔离性。

管理器在首次所属线程轮询前，关闭新任务入口；此时已注册使用方的任务已受理且仍待完成。
所有操作随后在自然排空期间完成，每次协程恢复及完成回调都必须使用所属线程与共享执行器。
成功且幂等的 stop 会执行插件正常的模块销毁；宿主作用域销毁管理器及运行时后，再检查保留结果。
这不代表操作系统动态库卸载、热更新、取消、故障恢复或容量验收。

缺少或无效配置、构建中没有请求的协议时，程序在执行服务操作前失败。
程序不启动服务，也不打印凭据或原生服务端文本。编译与这些失败检查不能证明真实服务 DSO
正向验收通过；需要显式选择模式，在隔离服务上实际运行。

新增可选 `redis-tls`、`redis-tls-untrusted-ca` 与 `redis-tls-wrong-host` 模式使用真实认证 TLS 服务。[Redis TLS DSO 指南](redis_tls_CN.md)说明可信 IP／主机名对照、私有服务要求及独立 PING／ECHO 计数观察。`check-database` 编译但不执行这些模式。

## 真实插件请求取消

独立的 `ark_database_public_plugin_cancellation_tests` 手动启用模式使用真实 Redis／MySQL 插件 DSO 与自有服务，先观察带标记的服务端工作，再调用 `task_group::cancel(id)`，保留原生取消／未知结果，并检查所属线程完成和自然退出。证据仅覆盖观察到的 ID、影响及有限观察窗口；MySQL 最终计数为零或一，不证明回滚，也不覆盖全部重试竞争。`check-database` 只编译该程序，不启动服务。构建命令、可信 TLS、独占写入数据与准确边界见[取消验收指南](cancellation_CN.md)。

## 公开模块真实服务负面验收

独立的 `ark_database_public_failure_service_tests` 验证 Redis/MySQL 错误密码、
MySQL 不可信 CA，以及真实慢命令的截止时间。`check-database` 仅编译该程序，不运行。
需显式选择负面验收模式；`all` 不包含这些模式。

```sh
cmake --build database-build --target ark_database_public_failure_service_tests --parallel 2
bash scripts/test_databases.sh database-build public-redis-failures
bash scripts/test_databases.sh database-build public-mysql-failures
# 也可以直接运行程序，参数为 redis 或 mysql。
```

沿用上方公开服务验收的隔离端点环境变量。Redis 负面模式额外要求非空的
`ARK_DATABASE_REDIS_PASSWORD`，账号需有 `CLIENT LIST` 权限。MySQL 要求开启 SSL、
使用可信的 `ARK_DATABASE_MYSQL_CA`，且账号能通过 `information_schema.processlist`
查看自身会话；不需要 PROCESS 权限或额外数据库。不可信 CA 使用本地 TLS 回归已有的
`ARKNET_SOURCE_DIR/tests/certs/wrong.pem`，不引入新的环境变量或服务管理操作。
配置缺失、观察连接权限不足或构建中没有请求的协议时，程序返回非零状态。

认证/TLS 失败必须保留原生错误，以及业务命令尚未发送的已知状态。随后使用正确配置显式发起
新请求。预热后的主连接对不存在的唯一 `ark_failure_...` key 执行 BLPOP（服务端阻塞
30 秒），或执行带唯一注释的 `SELECT SLEEP(10)`，客户端截止时间均为 2 秒。
测试先检查 Redis key 不存在，且不会向该 key 写入数据。两种超时结果均须在 5 秒内交付。

独立配置的观察连接通过 Redis `CLIENT LIST` 确认具名客户端正在阻塞，或通过 MySQL
`processlist` 确认带唯一注释的会话正在休眠。只有服务端可见状态用于确认请求已经发送；
轮询定时器仅控制观察频率。超时结果必须保留执行状态未知标志。随后通过同一公开连接名显式
执行 PING/SELECT，并验证恢复成功。观察连接拒绝出现第二个慢客户端或会话，且在原请求消失后
继续检查 500 毫秒。该证据仅覆盖观察窗口内的慢查询恢复，不证明写操作的重放行为，也不证明
窗口以外没有后续活动。

所有协程及任务完成回调均须在拥有线程及共享手动运行时上执行。管理器进入自然排空状态后，
已受理的慢请求和观察任务继续执行，完成后才退出原生资源，并验证停止操作可重复调用。
这些用例不测试取消、服务端终止、动态库卸载或容量。模块销毁后，慢查询结果仍须有效。
程序不打印凭据、原生错误文本或服务端错误文本。

## 本机公开接口服务验收

测试机器、源码版本与执行结果见[数据库服务验收](../../docs/guides/database-validation_CN.md)。

## 示例插件装配

`check-database-samples` 是 `check-database` 的依赖。它将真实 Redis 示例模块与插件装配一起构建为 `ark_database_redis_sample_check`，并运行 `ark_database_redis_sample_tests`。开启 `ARKNET_ENABLE_SSL=ON` 时，同一路径还会构建并运行 `ark_database_mysql_sample_check` 与 `ark_database_mysql_sample_tests`；关闭 SSL 时继续排除 MySQL。

```sh
cmake --build database-build --target check-database-samples --parallel 2
```

每个 runner 都通过管理器的标准入口与退出符号加载真实 DSO。fixture 注册公开数据库接口，并拒绝第一次配置请求。示例必须查找到该接口，保留 `primary`（MySQL）或 `sample`（Redis）连接名，并报告受控的 init 失败，诊断中包含具体示例模块名。真实插件退出必须成功返回，管理器必须进入 stopped 状态，重复 stop 必须成功。runner 仅使用管理器公开 API，不在停机后检查模块注册表，也不独立证明注册表条目已被移除。编译期检查保留类型基类与继承的默认 update 行为；装配显式使用 `module_update::disabled`。

这些测试替代仅编译 module.cpp 的检查：即使应用与示例关闭，插件入口／退出源文件仍是必需依赖。保留源码原有许可证，不启用其他示例。拒绝配置的 provider 不运行示例协程，也不连接服务；真实 MySQL／Redis 业务流程与部署仍需单独验收。

## 自有服务异常退出验收

可选的[公开插件服务异常退出指南](server_termination_CN.md)覆盖真实服务观察到的只读阻塞请求、
独立核验自有 PID 后的故障注入、原生传输失败及本地仅完成一次的退出。
常规检查只编译程序，不执行服务故障。

## Redis 重启恢复验收

可选的[重启操作手册](restart_CN.md)说明私有服务配置、故障协调与清理步骤。
[简短指南](../../docs/guides/database-restart_CN.md)汇总原池恢复行为与本地结果。
常规检查只编译该程序。
