# 访客房间示例

[English](README.md)

`guest_room_plugin` 通过真实 ARK 启动器及 Utility、Kernel、Bus、Net 插件运行一个二维访客大厅。每个已加入的会话拥有一个 `GuestPlayer` Kernel 实体；移动更新服务端实体，快照读取实体的名称、位置和序列号自定义字段。序列号使用 Kernel 已支持的自定义 int64 setter，协议仍限制为无符号 32 位范围。Net 回调及业务状态修改均在启动器所属线程执行。现有 Login 和默认 CS／SS 帧格式不变，无需 Registry、Consul、数据库、账号或持久化服务。

## 构建与运行

准备仓库的常规原生依赖，以及现有可选依赖 `nlohmann_json` 的 CMake 包。在同一次构建中启用 applications、examples 和本示例：

```sh
cmake -S . -B app-build -DARK_BUILD_APPLICATIONS=ON -DARK_BUILD_EXAMPLES=ON -DARK_BUILD_GUEST_ROOM_EXAMPLE=ON
cmake --build app-build --target app guest_room_plugin ark_kernel_plugin ark_utility_plugin ark_bus_plugin ark_net_plugin --parallel 1
./examples/guest_room/run.sh --bin-dir "$PWD/app-build/bin" --runtime-dir "$PWD/run/guest_room" --host 127.0.0.1 --port 19100
```

配置时仍需传入正常的依赖工具链或前缀参数；使用 vcpkg 时选择 `guest-room` 清单特性。Debug 构建的运行命令增加 `--debug`，使用 `app_d` 和 `_d` 插件。脚本复制同次构建的完整产物目录、TOML 清单和示例元数据。`--host` 指定本机绑定地址或主机名，`--port` 范围为 `1..65535`。使用新的运行目录，以 Ctrl+C 停服。停服超过五秒时，启动器保留资源并报告失败。

跨平台配置步骤如下：

```sh
cmake -DBIN_DIR=/absolute/app-build/bin -DRUNTIME_DIR=/absolute/run/guest_room -DHOST=127.0.0.1 -DPORT=19100 -P examples/guest_room/stage.cmake
cd /absolute/run/guest_room
./bin/app --busid=1.1.31.1 --name=guest_room --app_conf=conf/guest_room.app.toml --log_conf=conf/log.conf --shutdown_timeout_ms=5000
```

Windows 使用 `app.exe`／`app_d.exe`。C# Godot 示例连接相同主机和端口，移动发送频率约为 10 Hz。断线重连后，客户端须清空旧帧和房间状态，重新加入并接收新的玩家 ID。字段和错误约定见[协议](protocol_CN.md)；[wire-vectors.json](wire-vectors.json) 与 C# 测试使用相同的线格式向量。

## 原生验证

```sh
cmake --build app-build --target check-guest-room --parallel 1
```

`check-guest-room-codec` 使用真实 Net 会话解析器，检查共享线格式向量、每个单一拆分位置、粘包、4096／4097 字节边界、错误标记和 flags、全新解析状态，以及数字和名称边界。POSIX 系统上的 `check-guest-room-server` 启动真实 `app` 并加载插件动态库，通过两个原生 TCP 客户端检查加入、双向权威移动、过期与非法请求、有界错误、断线后的名单缩减、重新加入、非法帧断开及正常停服。测试使用独立运行目录和本机端口 19191；同一时间仅运行一份。进程测试采用 POSIX 子进程控制，不构成 Windows 运行验收。

示例将帧正文限制为 4096 字节，连接数限制为 32，房间人数限制为 16；每轮最多处理 64 次业务回调，每个连接每秒最多 32 条消息，每个连接的业务待发送队列最多保留八帧。超过工作量或输出队列限制时关闭对应连接。JSON 嵌套最多八层。示例沿用底层传输的发送队列限制，发送被拒绝时关闭对应连接。这些是演示限制，原生测试不代表完整的恶意客户端加固、生产容量、公网部署或 Godot 视觉验收。
