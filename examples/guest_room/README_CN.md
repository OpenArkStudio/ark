# 访客房间示例

[English](README.md)

`guest_room_plugin` 通过真实 ARK 启动器及 Utility、Kernel、Bus、Net 插件运行一个二维访客大厅。每个已加入的会话拥有一个 `GuestPlayer` Kernel Entity，正式字段及初始配置来自 JSON。移动和 HP／MP 操作更新该 Entity，所属线程的 flush 捕获选定字段，并发布 Protobuf 基线／增量。Net 回调及业务状态修改均在启动器所属线程执行。现有 Login、默认 CS／SS 帧格式及其 4096 字节上限不变，无需 Registry、Consul、数据库、账号或持久化服务。

匹配的 C# Godot 客户端使用 ARK2，并显示 HP／MP。`damage` 将自身 HP 减少 15，`skill` 在 MP 足够时消耗 20 MP，`rest` 将 HP／MP 恢复 20，最多恢复到配置上限。HP 为零仍可移动。数值和操作目标由服务端确定，客户端只请求操作类型和独立的操作序列号。详见[协议及迁移说明](protocol_CN.md)。

## Entity 定义与初始配置

[resource/meta/entity_schema.json](resource/meta/entity_schema.json) 是 Kernel 的权威字段定义与静态配置。文件声明必需的空 `Player` 类，将 `GuestPlayer` 字段固定在索引 100..108：`name`、`x`、`y`、`move_seq`、`hp`、`mp`、`hp_max`、`mp_max`、`action_seq`，并将配置标识 `id` 放在索引 99。名称是字符串，坐标是 double，序列号和属性是 uint32。这取代了示例中占位的 XML 元数据及自定义字段。

`GuestPlayer` 的 `id = 1` 配置行提供全部标量值，包括初始为 100 的 HP／MP／上限及初始为零的两类序列号。加入时调用 `create_entity(..., "GuestPlayer", 1, ...)`，再通过普通 Entity setter 设置会话名称和出生坐标。字段定义确定类型及索引，业务 JSON 配置行提供数值；代码不硬编码替代属性默认值，也不在运行时修改字段定义。配置 ID 与快照中的不透明会话玩家 ID 分别使用。Schema 中的 mask 是元数据；模块显式选择公开索引 100..108，使用 `capture_scalar_snapshot` 与 `scalar_publisher`。新连接接收完整基线，已有连接接收新增基线、修改字段及墓碑。移动和操作在所属线程每次 flush 前合并。

通过框架的 [JSON 实体定义与业务配置流程](../../docs/reference/kernel_CN.md#json-实体定义与业务配置)修改配置，导出有效 JSON，复制到运行目录后重启示例。HP／MP 上限须为正数，当前值不得超过对应上限，初始序列号须为零。Kernel 在该文件存在时选择它，内容非法时直接失败，不回退到 XML。示例在 `pre_update()` 阶段检查必需字段的索引／类型及配置行，确保各模块已在 `post_init()` 中加载静态配置，再开放监听。重启或重新加入创建全新状态，不支持热重载或持久化。

## 构建与运行

准备仓库的常规原生依赖，以及 Protobuf 和现有可选依赖 `nlohmann_json` 的 CMake 包。在同一次构建中启用 applications、examples 和本示例：

```sh
cmake -S . -B app-build -DARK_BUILD_APPLICATIONS=ON -DARK_BUILD_EXAMPLES=ON -DARK_BUILD_GUEST_ROOM_EXAMPLE=ON -DARK_BUILD_PROTO=ON
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

Windows 使用 `app.exe`／`app_d.exe`。官方 [C# Godot 客户端](https://github.com/OpenArkStudio/ark-client-godot)连接相同主机和端口，移动发送频率约为 10 Hz。断线重连后，客户端须清空旧帧和房间状态，重新加入并接收新的玩家 ID 和实际 Entity GUID。字段和错误约定见[协议](protocol_CN.md)；[wire-vectors.json](wire-vectors.json) 与 C# 测试使用相同的线格式向量。

## 原生验证

```sh
cmake --build app-build --target check-guest-room --parallel 1
```

`check-guest-room-codec` 使用真实 Net 解析器检查共享 Protobuf 正文／帧十六进制向量、每个拆分位置、粘包、8192／8193 字节边界、头部、全新解析状态及严格 UTF-8 名称。适配器测试覆盖九种标量分支、整数边界、任意 bytes、零值存在性、重复字段、上限、畸形编码、公开投影类型、版本缺口、重复增量、墓碑及 revision 耗尽。16 人最坏 Protobuf 基线须符合帧预算。

POSIX 上的 `check-guest-room-server` 启动真实启动器／插件动态库和两个原生 TCP 客户端，检查 Accepted／Entity 绑定、晚加入完整基线、已有连接收到新增基线、合并移动增量、HP／MP 操作、边界操作只确认序列号、状态未变化时无输出、错误不修改状态、墓碑、重连新 Entity GUID、畸形帧断开及正常停服。额外进程加载接近 UINT32_MAX 及 19 MP 的配置，并拒绝非法初始配置。测试使用独立运行目录和端口 19191，同一时间仅运行一份。该进程控制器不构成 Windows 或 C# Godot 验收。

Applications 工作流在现有平台矩阵中启用本可选示例。默认构建在 Linux、macOS 和 Windows 编译插件；`check-core` 在各平台运行 codec 检查，并在 POSIX 系统运行真实启动器与双客户端进程检查。工作流配置与某次托管运行的结果分别验收。

示例限制为 8192 字节正文、32 个连接及 16 名玩家；每轮最多处理 64 次业务回调，每连接每秒最多 32 条消息，待发送队列最多八帧及 `8 * (8192 + 12)` 个自有字节。工作量／输出超限或底层发送被拒绝时关闭连接。捕获状态不支持或 revision 耗尽时须重连并获取新基线。这些限制及原生测试不代表生产容量、部署或 Godot 视觉验收。兼容性、投影、版本及恢复规则见[协议](protocol_CN.md)。
