# 真实 Redis TLS 插件验收

[English](redis_tls.md)

该可选独立 C++ 程序通过 `plugin_manager` 加载真实隐藏符号 Redis 插件 DSO，仅调用公开 Redis／管理器／任务接口，不写入键。程序不启动或停止服务。`check-database` 编译程序但不执行这些真实服务模式；现有 `redis_service_tls.cpp` 协议端点测试仍作为独立回归自动运行。

## 构建与自有服务要求

按[数据库构建指南](README_CN.md#构建)启用 `ARK_BUILD_DATABASE_TESTS=ON`、`ARKNET_ENABLE_SSL=ON`，使用固定版本的本地 arknet 源码，然后构建：

```sh
cmake --build database-build --target ark_database_public_plugin_service_tests --parallel 1
```

准备独占的一次性 Redis 实例：使用全新私有数据目录、随机回环 TLS 端口与认证，禁用持久化及明文端口（`port 0`）。使用现有 OpenSSL 工具链，以 `BUILD_TLS=yes` 构建 Redis。不修改共享或已安装的服务。凭据、私钥及服务配置保存在私有位置；密码仅通过环境传入，不放入参数或日志。使用前及认证执行 `SHUTDOWN NOSAVE` 前，核对自有子进程的 PID、可执行文件、私有目录、PID 文件及该 PID 的监听；所有结果下均应回收子进程并删除私有数据与秘密。

为主机名对照，由测试 CA 签发服务证书，**仅包含 `127.0.0.1` 的 IP SAN**，不包含 `localhost` 的 DNS SAN 或匹配的通用名称。测试前通过 OpenSSL 确认证书链与 IP。`localhost` 必须解析到同一回环服务；普通连接拒绝不能使该模式通过。另备一张有效但不相关的 CA 证书，用于不可信 CA 模式。本次不验证客户端证书认证（`tls-auth-clients no`），可信 TLS 上仍要求密码认证。

在私有测试环境中设置：

| 变量 | 含义 |
| --- | --- |
| `ARK_DATABASE_REDIS_HOST` | `127.0.0.1`，与服务证书 IP SAN 匹配 |
| `ARK_DATABASE_REDIS_PORT` | 自有实例的 TLS 端口 |
| `ARK_DATABASE_REDIS_PASSWORD` | 必填非空密码 |
| `ARK_DATABASE_REDIS_USER` | 可选 ACL 用户名 |
| `ARK_DATABASE_REDIS_CA` | 可信 CA PEM 文件 |
| `ARK_DATABASE_REDIS_UNTRUSTED_CA` | 有效但不相关的 CA PEM 文件 |
| `ARK_DATABASE_REDIS_WRONG_HOST` | `localhost`，解析到同一服务，但不在证书中 |

```sh
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls-untrusted-ca
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls-wrong-host
```

## 断言与观察边界

正常模式通过认证 TLS 执行 ECHO 命令，保留拥有型二进制回复，再验证二进制／空 ECHO 与 PING 流水线的回复顺序。调用方修改 host／password／CA 后，配置副本仍生效；较早的命令结果在后续流水线完成后仍可读取。模块／管理器／运行时销毁及原生 DSO 关闭后，两份拥有型结果仍可读取。

两个拒绝模式尝试 PING，但必须保留原生 `client_errc::connection_failed`、OpenSSL 证书校验传输错误、`command_may_have_run=false` 及空回复。连接拒绝、超时、SQL／Redis 服务错误或协程异常均不能作为通过结果。销毁后再次检查拥有型错误。宿主任务在所属线程／共享执行器上完成一次；普通 `manager.stop()/update()` 排空已接收工作、完成原生服务退出，并支持重复 stop。测试观察时限不使协作式 stop 成为强制或抢占式生产期限。

独立观察业务命令时，在每个拒绝模式执行前后，立即通过认证的 `redis-cli --tls --cacert` 获取 `INFO commandstats`；该窗口内不发送 PING／ECHO，也不运行其他客户端。比较 `cmdstat_ping` 与 `cmdstat_echo` 的调用数；INFO 自身会改变其他统计。计数不变仅支持该 PING／ECHO 观察窗口。C++ 程序不读取服务计数，单独运行不能证明真实服务未收到任何 RESP 命令；有界协议端点回归另行统计所有接收到的 RESP 命令。

这些模式不验证双向 TLS、证书轮换／撤销、TLS 取消／服务故障／恢复、持久化、跨机器容量或其他平台。原生关闭成功不证明操作系统已物理移除库映像，也不证明任意热更新安全。
