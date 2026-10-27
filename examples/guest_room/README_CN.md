# 访客房间示例

[English](README.md)

`guest_room_plugin` 通过真实 ARK 启动器及 Utility、Kernel、Bus、Net 插件运行一个二维访客大厅。每个已加入的会话拥有一个 `GuestPlayer` Kernel Entity，正式字段及初始配置来自 JSON。移动和 HP／MP 操作更新该 Entity，快照读取按名称访问的定义字段。Net 回调及业务状态修改均在启动器所属线程执行。现有 Login、默认 CS／SS 帧格式及其 4096 字节上限不变，无需 Registry、Consul、数据库、账号或持久化服务。

匹配的 C# Godot 客户端使用 ARK2，并显示 HP／MP。`damage` 将自身 HP 减少 15，`skill` 在 MP 足够时消耗 20 MP，`rest` 将 HP／MP 恢复 20，最多恢复到配置上限。HP 为零仍可移动。数值和操作目标由服务端确定，客户端只请求操作类型和独立的操作序列号。详见[协议及 v1 迁移说明](protocol_CN.md)。

## Entity 定义与初始配置

[resource/meta/entity_schema.json](resource/meta/entity_schema.json) 是 Kernel 的权威字段定义与静态配置。文件声明必需的空 `Player` 类，将 `GuestPlayer` 字段固定在索引 100..108：`name`、`x`、`y`、`move_seq`、`hp`、`mp`、`hp_max`、`mp_max`、`action_seq`，并将配置标识 `id` 放在索引 99。名称是字符串，坐标是 double，序列号和属性是 uint32。这取代了示例中占位的 XML 元数据及自定义字段。

`GuestPlayer` 的 `id = 1` 配置行提供全部标量值，包括初始为 100 的 HP／MP／上限及初始为零的两类序列号。加入时调用 `create_entity(..., "GuestPlayer", 1, ...)`，再通过普通 Entity setter 设置会话名称和出生坐标。字段定义确定类型及索引，业务 JSON 配置行提供数值；代码不硬编码替代属性默认值，也不在运行时修改字段定义。配置 ID 与快照中的不透明会话玩家 ID 分别使用。Schema 中的 mask 是元数据；示例模块在接受修改后显式构建并广播完整快照，不代表已开启 Entity 自动复制。

通过框架的 [JSON 实体定义与业务配置流程](../../docs/reference/kernel_CN.md#json-实体定义与业务配置)修改配置，导出有效 JSON，复制到运行目录后重启示例。HP／MP 不得超过对应上限，初始序列号须为零。Kernel 在该文件存在时选择它，内容非法时直接失败，不回退到 XML。示例在 `pre_update()` 阶段检查必需字段的索引／类型及配置行，确保各模块已在 `post_init()` 中加载静态配置，再开放监听。重启或重新加入创建全新状态，不支持热重载或持久化。

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

Windows 使用 `app.exe`／`app_d.exe`。官方 [C# Godot 客户端](https://github.com/OpenArkStudio/ark-client-godot)连接相同主机和端口，移动发送频率约为 10 Hz。断线重连后，客户端须清空旧帧和房间状态，重新加入并接收新的玩家 ID。字段和错误约定见[协议](protocol_CN.md)；[wire-vectors.json](wire-vectors.json) 与 C# 测试使用相同的线格式向量。

## 原生验证

```sh
cmake --build app-build --target check-guest-room --parallel 1
```

`check-guest-room-codec` 使用真实 Net 会话解析器，检查共享 ARK2 线格式向量、每个单一拆分位置、粘包、8192／8193 字节边界、旧标记拒绝、错误标记和 flags、全新解析状态，以及数字和名称边界。16 人最坏快照包括转义名称、20 位玩家 ID、double 坐标和 uint32 属性，正文超过旧版 4096 字节预算。

POSIX 系统上的 `check-guest-room-server` 启动真实 `app` 并加载插件动态库，通过原生 TCP 客户端检查加入、权威移动与属性操作、独立序列号、非法／过期／资源不足错误不修改状态、到达边界后的无数值变化确认、HP 为零时移动、重新加入后加载初始配置、非法帧断开及正常停服。额外启动加载接近 UINT32_MAX 及 MP 为 19 的 JSON 配置，检查恢复操作不会溢出及资源边界；初始属性超限或序列号非零时须启动失败。测试使用独立运行目录和本机端口 19191；同一时间仅运行一份。进程控制器不构成 Windows 运行验收。

Applications 工作流在现有平台矩阵中启用本可选示例。默认构建在 Linux、macOS 和 Windows 编译插件；`check-core` 在各平台运行 codec 检查，并在 POSIX 系统运行真实启动器与双客户端进程检查。工作流配置与某次托管运行的结果分别验收。

示例将帧正文限制为 8192 字节，连接数限制为 32，房间人数限制为 16；每轮最多处理 64 次业务回调，每个连接每秒最多 32 条消息，每个连接的业务待发送队列最多保留八帧。超过工作量或输出队列限制时关闭对应连接。JSON 嵌套最多八层。示例沿用底层传输的发送队列限制，发送被拒绝时关闭对应连接。这些是演示限制，原生测试不代表完整的恶意客户端加固、生产容量、公网部署或 Godot 视觉验收。
