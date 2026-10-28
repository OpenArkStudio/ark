# ARK 访客房间 Protobuf 协议

[English](protocol.md)

[guest_room.proto](../../src/proto/proto/guest_room.proto) 是权威契约，包名为 `ark.protobuf.guest_room`，C# 命名空间为 `Ark.Client.Protocol`。两端从该 schema 生成消息。应用协议版本为 `1`，Join 和 Accepted 均须携带。服务端与 Godot 客户端须一起升级：传输标记仍为 `ARK2`，但原有 ARK2 JSON 正文已不兼容。服务端 Entity 字段定义与配置仍使用 JSON。

TCP 帧使用固定的 12 字节头部：第 `0..3` 字节为 ASCII `ARK2`，第 `4..5` 字节为大端无符号 16 位消息 ID，第 `6..7` 字节为必须为零的 16 位 flags，第 `8..11` 字节为大端无符号 32 位正文长度。正文长度不含头部，最多为 8192 字节。正文为不压缩的 Protobuf。示例头部上限不改变原生 CS／SS 头部及其 4096 字节默认上限。标记、flags 或长度非法时，在业务回调之前关闭连接。解析器保留不完整帧并分别解析粘包；每个连接使用全新的解析状态。

| ID | 方向 | Protobuf 消息 |
| --- | --- | --- |
| 60000 | 客户端 → 服务端 | `join_request`：name、room、protocol_version |
| 60001 | 服务端 → 加入者 | `join_accepted`：逻辑 player_id、名称、房间、尺寸、实际 entity_id、protocol_version |
| 60002 | 客户端 → 服务端 | `move_request`：sequence、x、y |
| 60003 | 服务端 → 加入者 | `room_baseline`：当前完整授权名单 |
| 60004 | 服务端 → 客户端 | `error_response`：code、message、可选 action_sequence |
| 60005／60006 | Ping／pong | `heartbeat`：uint32 nonce |
| 60008 | 客户端 → 服务端 | `action_request`：sequence 与操作枚举 |
| 60009 | 服务端 → 已加入者 | `room_delta`：新增基线及修改／删除增量 |

60007 仅用于内部不支持消息的分发，不向网络发出。客户端仅可发送 Join、Move、Ping 和 Action。未知／方向错误的消息 ID 及畸形 Protobuf 会关闭连接。允许 Protobuf 未知字段以支持向前兼容；未知操作枚举返回业务错误。错误码包括 `room_full`、`already_joined`、`not_joined`、`invalid_message`、`stale_sequence` 和 `insufficient_resource`。未超过工作量／输出限制时，业务错误保留连接。操作错误携带 `action_sequence`，缺少序列号时为零；其他错误不携带该字段。

仅提供一个 `lobby`，最多容纳 16 名玩家，不进行账号认证。名称须为 `1..64` 个有效 UTF-8 字节，且不含 Unicode C0／C1 控制字符。坐标须为有限值，分别位于 `0..960`／`0..540` 内；出生位置由服务端确定。移动及属性操作分别使用从 1 开始、独立递增的 uint32 序列号。玩家 ID 是进程内唯一的不透明十进制字符串；Entity GUID 标识实际 Kernel 实例。Accepted 将请求会话的两个标识绑定。请求不能通过提供身份选择或授权修改 Entity。

## 公开 Entity 投影

公开类为 `GuestPlayer`，schema ID 为 `ark.guest-player.v1`。每份玩家基线必须完整且仅包含以下字段；索引 99 的配置 ID 及服务端私有字段不对外同步。

| 索引 | 字段 | Protobuf 标量分支 |
| --- | --- | --- |
| 100 | name | `string_value` 字节，并按上述名称规则验证 |
| 101／102 | x／y | `double_value` |
| 103 | move_seq | `uint32_value` |
| 104／105 | hp／mp | `uint32_value` |
| 106／107 | hp_max／mp_max | `uint32_value` |
| 108 | action_seq | `uint32_value` |

通用标量 oneof 明确区分 bool、sint32、uint32、sint64、uint64、float、double、bytes 和有符号 64 位 GUID；零值仍保留分支存在性。通用适配器的 bytes 可包含任意字节，公开名称另行应用严格 UTF-8 规则。通用 GUID 值保留原生负值，受有符号 64 位线格式及原生位宽限制。房间 Entity 身份仍为正 uint64 值，并限制在原生 `guid_t` 范围。可移植客户端在提交名单修改前验证完整房间消息，包括投影／类型、身份、schema／类、有限坐标、正 HP／MP 上限及当前值范围。

房间所属线程在 update 时，为每个活跃 Entity 调用一次真实 `kernel.capture_scalar_snapshot`，再由 `scalar_publisher` 比较选定字段。新连接先收到 Accepted，再收到完整 RoomBaseline。已有连接通过 RoomDelta 收到新增玩家基线、修改字段或 `removed = true` 墓碑。接受 Move／Action 时不立即广播完整名单。同一次 flush 前的修改合并；新增基线已包含最终状态，不再附带中间增量。状态未变化时不发增量。发布前即创建并删除的 Entity 可以不发消息。

每个 Entity 发布器从 generation 1、revision 1 开始。增量保持相同 generation 和实际 Entity GUID，`required_revision` 等于前一版本，且 `revision = required_revision + 1`。删除也推进 revision，不携带字段。版本缺口、schema／类型非法或代际倒退需要通过新连接／基线恢复；重复增量不得使状态回退，墓碑阻止旧实例复活。不提供 ACK、重放或持久化副本历史。重连清空旧解析／副本状态，以新的逻辑 ID 和 Entity GUID 加入并安装完整基线。

## 服务端操作与限制

`ACTION_DAMAGE` 将自身 HP 减少 15，最低为零。`ACTION_SKILL` 要求并消耗 20 MP。`ACTION_REST` 恢复 20 HP／MP，不超过配置上限，且不发生 uint32 溢出。拒绝 `ACTION_UNSPECIFIED` 及未知枚举。初始值来自配置行 1，随示例提供的 HP／MP／上限均为 100，两类序列号均为零。HP 为零仍可移动。数值已到边界的操作仍推进 `action_seq`，因此增量可以仅包含索引 108。非法／过期／资源不足的操作不修改字段或序列号。客户端仅根据匹配的错误或权威序列号确认结束操作等待，不自动重放。

示例限制为 32 个连接、16 名已加入玩家、所属线程每轮 64 次业务回调、每连接每秒 32 条消息。每连接最多持有八个待发送帧及 `8 * (8192 + 12)` 个待发送字节。工作量／队列超限、序列化／入队／发送失败、捕获状态不支持或 revision 耗尽时关闭相关会话；流级捕获失败时关闭已加入会话，避免继续发布部分状态。仍受底层传输发送队列限制。这些演示限制不代表生产容量、完整恶意客户端防护或 Godot 视觉验收。

[wire-vectors.json](wire-vectors.json) 为 C++ 与 C# 提供共享 Protobuf 正文／帧十六进制向量，包含完整基线、HP 增量及墓碑。服务端和客户端仓库中的 schema 与向量须逐字节一致。
