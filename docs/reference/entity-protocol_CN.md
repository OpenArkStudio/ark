# Entity 复制协议

`<ark/kernel/nested_wire.hpp>` 将拥有型[嵌套发布](kernel_CN.md#嵌套基线与增量)
与规范 [`entity_sync.proto`](../../src/proto/proto/entity_sync.proto) 相互转换。
启用 `ARK_BUILD_PROTO=ON`，并链接 `ark::kernel` 和 `ark::proto`。
生成的 C++ 消息使用 `ark::protobuf::entity_sync`，C# 消息使用
`Ark.Client.Protocol.EntitySync`。

<Aside type="note" title="编解码与客户端验收">
编解码器按应用绑定验证转换。使用它本身不证明 Godot 传输、房间业务规则、渲染或实际
多客户端会话已通过验收；应用分别提供这些集成及其检查。
</Aside>

## 先绑定，再接收状态

应用在接收任何远端基线前提供固定的 `nested_stream_binding`：Schema ID、根 GUID／
实例标记、流代次及已授权的 `nested_entity_layout`。布局包括空表的字段类型和每个
容器逐个允许的子 GUID。状态消息不能定义或扩大该绑定。应用负责会话认证和投影授权。

```cpp
#include <ark/kernel/nested_wire.hpp>

using namespace ark::entity_sync;

// trusted 和 initial 来自应用已授权的捕获路径。
nested_wire::limits budget;
nested_publisher sender(trusted, initial, budget.stream);
nested_replica receiver(trusted, budget.stream);

std::string body = nested_wire::encode(*sender.baseline(), trusted, budget);
auto baseline = nested_wire::decode_baseline(body, trusted, budget);
auto result = receiver.apply(baseline);
```

| `ark::entity_sync::nested_wire` 中的 API | 结果 |
| --- | --- |
| `encode(native, proto_output, trusted, limits)` | 基线／增量重载校验并暂存生成的消息，完成后交换到输出 |
| `decode(proto_input, trusted, limits)` | 基线／增量重载返回新的拥有型原生发布 |
| `encode(native, trusted, limits)` | 返回 `std::string` Protobuf 消息体，不添加传输头 |
| `decode_baseline(bytes, trusted, limits)` / `decode_delta(bytes, trusted, limits)` | 解析有界的 `std::string_view` 消息体并校验发布 |

`limits` 参数可省略。调用方按传输契约区分基线与增量。编解码器不认证会话、不管理
发送队列，也不应用发布。返回发布不代表已经交付。

## 值与结构

`scalar_field.value` 使用显式 oneof；已选择的类型分支保留零值和 false 的存在性。
C++ variant 索引与 `nested_scalar_type` 序号不是线协议 ABI。

| 原生值 | Protobuf 分支 | 约束 |
| --- | --- | --- |
| `bool` | `bool_value` | 显式 false 也是已存在的值 |
| `int32_t` / `uint32_t` | `int32_value`（`sint32`）/ `uint32_value` | 保留符号与宽度 |
| `int64_t` / `uint64_t` | `int64_value`（`sint64`）/ `uint64_value` | 保留完整原生范围 |
| `float` / `double` | `float_value` / `double_value` | 仅允许有限值 |
| `std::string` | `string_value`（`bytes`）| 原始字节，包括 NUL 和非 UTF-8；展示层自行校验文本规则 |
| `guid_value` | `guid_value`（`sint64`）| 同时满足有符号 64 位线协议范围及原生 `guid_t` 范围 |

Entity 身份也使用 `sint64`，且不得为零；标量 GUID 值可以为零。默认原生 GUID 为
有符号 64 位；32 位构建在解码时拒绝窄化，较宽构建在编码时拒绝线协议无法表示的值。

| 发布 | 内容 |
| --- | --- |
| `nested_baseline` | 流代次、非零修订号及完整 `nested_snapshot` |
| `nested_snapshot` | Schema ID 和根 Entity 快照 |
| Entity 快照 | GUID／实例标记、类名、完整的选定字段／表格／容器；保留已选择的空表和空容器 |
| 表格快照 | 成员索引、行类及正行号索引的行，每行包含实例标记和全部选定列 |
| `nested_delta` | Schema ID、流代次、基准修订、下一修订、根补丁及可选整根删除标志 |
| Entity 补丁 | GUID／实例标记及变化的标量、表格行和容器记录 |

索引集合使用 repeated 记录。解码器在安装任何状态前拒绝重复字段／表格／容器索引、
行号及槽位。索引在类内解析，表格列属于行类。完整根拒绝重复 Entity GUID、共享子
引用和循环。未知 Protobuf 字段可以在同一字节／递归上限内被忽略，不授予任何权限。

## 行与容器变化

| `row_delta` 实例标记 | 含义 |
| --- | --- |
| `required_incarnation == 0`，`incarnation > 0` | 完整提供所有选定列，新增原先不存在的行 |
| `required_incarnation > 0`，`incarnation == 0` | 移除匹配的行，fields 必须为空 |
| 两者非零且相等 | 修改非空的选定列子集 |
| 新标记大于非零基准标记 | 完整提供所有选定列，替换匹配的行 |

行身份包括所属 Entity 存续期、表格索引、行号及实例标记。删除后复用行号，即使两次
刷新之间最终值相同，也必须分配新标记。接收端核对基准身份并记录各表的实例标记水位，
已退役行存续期不能重新引入。

`container_delta.update` 恰好选择一种表示：

| 分支 | 含义 |
| --- | --- |
| `replacement` | 最终授权槽位的完整集合，附带拥有型子快照；空列表清空容器 |
| `patch` | 非空的变化槽位列表，附带递归补丁；槽位原有子 GUID／实例标记必须匹配 |

槽位、子 GUID 或实例标记任一变化，发布端选择完整替换；否则递归修改子值。
槽位移动／交换及多个容器的变化在同一根中一起提交。解除子关系不等于声明源 Entity 已销毁。

## 提交与恢复

编解码器校验绑定、消息封装、类型、结构及发布上限，无法判定增量是否匹配接收端的
当前修订、重建状态和身份历史；须将结果传给 `nested_replica::apply()`。

| 接收端条件 | 结果 |
| --- | --- |
| 首个完整且匹配的基线 | 安装一次 |
| 新增量满足 `required_revision == current_revision` 且 `revision == required_revision + 1` | 验证私有完整候选和身份历史，再一次提交整根 |
| 结构合法且修订不高于当前修订的增量 | `duplicate`，状态不回退 |
| 旧基线／与当前有效状态相同的同修订基线 | `duplicate`；更新的基线不能用于原地重置 |
| 缺口、存续期／绑定不符或非法状态转换 | `rejected`，隐藏状态并永久使该接收端失效 |
| 有序整根删除标记 | 移除状态，保留有效的重复处理能力；后续流量不能复活根 |

应用在非法输入、溢出、编码失败或入队／发送拒绝时关闭受影响流。恢复需要新连接、
独立可信绑定及完整基线。固定投影不支持原地流代次重置或扩权。原子性仅覆盖一棵
选定根，不提供跨根事务、ACK 或重放日志。

## 独立上限与失败处理

| 预算 | 默认值／范围 |
| --- | --- |
| `limits.stream.snapshot` | 根深度为 0，Entity 深度 8；1024 个 Entity、65536 行、65536 个选定字段／表格／容器成员，1 MiB 的 Schema ID／类名／字节串值；绑定与完整状态分别核算 |
| 补丁工作量 | 65536 个 Entity／表格／容器／行操作、65536 个标量值及 1 MiB 的 Schema ID／类名／字节串值，包括替代子树 |
| `max_lifetime_entries` | 132096 条已记录的 Entity／表格／最近可见行身份；基线校验及有状态发布端／接收端均执行上限 |
| `max_message_bytes` | 4 MiB 编码后消息体，与原生状态和传输待发队列分别限制 |
| `max_recursion_depth` | 128 层 Protobuf 消息，含记录包装层和未知 group，区别于 Entity 深度 |

字节解析使用 `CodedInputStream` 设置总字节数和递归上限。生成消息重载也核对层数
和消息体大小。原生状态／补丁校验不测量精确堆用量；已解析消息、旧状态、候选状态和
历史副本可能同时存在。应用须分别限制待发帧数／字节数／年龄、保留发布、并行工作量
和内存峰值。向 arena 所有的输出编码时，暂存分配受该 arena 的生命周期约束，
失败尝试的分配也包括在内。

| 失败 | C++ 行为 |
| --- | --- |
| 缺失类型分支、重复索引、绑定／类型错误或非法字节 | 抛出 `std::invalid_argument` |
| 状态／补丁／消息体超限或生成消息层数超限 | 抛出 `std::length_error`；原始字节解析的递归失败报告 `std::invalid_argument` |
| 分配失败 | 向上传播 `std::bad_alloc`，不发布部分生成输出或原生返回值 |
| 有状态 `apply()` 拒绝已解码发布 | 返回 `rejected` 并丢弃当前接收状态；此前保留的不可变句柄仍可读取 |

编码在目标的 Protobuf arena 上暂存，完整校验后才交换。编解码异常不调用或使接收端
失效，应用负责关闭流和释放状态。接收端自身的分配失败另有保持旧提交状态／修订的
保证；这个本地保证不创建传输重试策略。

`check-nested-wire` 在默认及 32 位原生 GUID 构建下覆盖往返、全部标量类型、二进制值、
重复记录、非法结构、独立预算、arena 输出及有状态拒绝。这些属于本地 C++ 正确性
检查，客户端与网络集成仍需各自验收。
