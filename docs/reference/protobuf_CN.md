# Protobuf 消息与兼容边界

协议定义位于 `src/proto/proto/`。源码文件、消息、字段与枚举标识符采用 `snake_case`。原 `AFMsg` package 改为 `ark.protobuf`，C++ 命名空间为 `ark::protobuf`；`consul`、`osslog` 保留既有 package。

## 构建与包含

启用 `ARK_BUILD_PROTO=ON` 并使用 `ark::proto`，取得生成头文件路径、`ARK_HAS_PROTOBUF` 及 Protobuf 运行库依赖。构建应用时必须启用此选项。

```cmake
add_subdirectory(path/to/ark ark-build)
add_executable(my_service main.cpp)
target_link_libraries(my_service PRIVATE ark::core ark::proto)
```

生成的 C++ 文件保留在 CMake 构建目录的 `src/proto/generated/ark/proto/` 下。修改源码 `.proto` 后，由 CMake 重新生成，不直接编辑生成头文件。可包含 `<ark/proto/common_messages.pb.h>` 等单类消息头文件，或使用汇总头文件 `<ark/proto.hpp>`。`.pb.h` 后缀由 Protobuf 生成器决定，不应手动重命名。

Protobuf、Abseil 与 utf8_range 应来自同一套兼容安装。手工选择软件包时，同步检查 `Protobuf_DIR`、`absl_DIR` 与 `utf8_range_DIR`；链接成功本身不能证明反射和 JSON 行为正确。

Windows 上，`AFProto`／`ark::proto` 构建为单一生成消息共享库。宿主和加载的插件导入同一套消息类及描述符，包括宿主为公开 Kernel 转换接口创建消息的场景。CMake 通过 protoc 生成 `ARK_PROTO_EXPORT` 声明，并由 `ark::proto` 向消费者传递导入定义；不要把另一份生成的 `.pb.cc` 编入消费者，也不要手工定义导出宏。其他平台保留原有静态 AFProto 库和生成命令。

**Windows 二进制迁移：**一起重建宿主和插件，部署既有构建 `bin/` 中的 `AFProto.dll`（Debug 为 `AFProto_d.dll`）。Windows 宿主、插件和消息 DLL 必须使用同一套兼容的 Protobuf 运行时与构建配置。既有 RUNTIME 安装规则把 DLL 放到 `bin/`，导入库放到库目录。虽然 `.lib` 文件名保留，也不能混用旧静态 AFProto 归档与新导入库。此变更影响 Windows 链接和部署，不改变协议字段号或二进制编码。

`check-proto-configuration` 配置两个 CMake 分支并运行真实 protoc，检查消息／描述符导出声明、消费者导入定义及原有非 Windows 生成路径。它不在其他平台上编译或加载 Windows 二进制。`check-kernel-plugin` 保留宿主创建的 Protobuf 对象及真实跨 DSO 转换；Windows 加载验收仍要求该平台的原生运行。

## 消息序列化

消息直接使用 Protobuf 生成的 API。Bus 模块在 `src/plugins/bus/msg_module_impl.cpp` 的消息发送流程中调用 `SerializeToString`，序列化失败时返回 `false`。`<ark/bus/msg_module.hpp>` 中的公开辅助接口 `msg_module::decode_protobuf` 对网络消息体调用 `ParseFromString`，解析失败时返回 `false`，成功时复制 actor ID。消息与传输的边界见 [Bus](bus_CN.md)。

ark 不提供可替换的消息 codec API。现有序列化流程使用生成的消息与上述 Bus 辅助接口。

## 命名迁移约定

| 保持不变 | 调用方需要调整 |
| --- | --- |
| 字段号、类型与重复性 | C++ 类型名和字段访问方法 |
| 枚举数值与二进制消息编码 | descriptor 全名与 `Any` 类型 URL |
| 显式保留的外部 JSON 字段名 | 已改名的 JSON 枚举字符串及依赖文本类型名的代码 |

当前名称包括 `ark::protobuf::protobuf_data`、`protobuf_node_data`、`protobuf_entity_data`。相关辅助接口使用完整的 `protobuf`，不再使用 `pb` 缩写前缀，例如 Bus 模块的 `msg_module::decode_protobuf`。依赖的 C++ 代码需要与生成消息一同重新构建。

## ID 字段与访客操作枚举迁移

重新生成 Protobuf 源码，并将调用方切换到以下字段名。C++ getter、`set_`、`mutable_` 等生成访问接口随字段名变化；包名、消息名、字段号、类型及重复性保持不变。已有二进制消息仍按原 tag 解析。字段名反射及 Protobuf 文本格式的字段键改用新名称。

| 消息 | 原字段 | 新字段 | 保留的 JSON 键 | tag |
| --- | --- | --- | --- | --- |
| `req_switch_server`、`ack_switch_server` | `selfid` | `self_id` | `selfid` | 1 |
| 同上 | `self_serverid` | `self_server_id` | `selfServerid` | 2 |
| 同上 | `target_serverid` | `target_server_id` | `targetServerid` | 3 |
| 同上 | `gate_serverid` | `gate_server_id` | `gateServerid` | 4 |
| `mysql_parameters` | `nreqid` | `request_id` | `nreqid` | 6 |
| `osslog.login` | `logid` | `log_id` | `logid` | 1 |

上述旧 JSON 键现在通过显式 `json_name` 保留，包括原先隐式生成的驼峰键。既有 `SceneID`、`groupID`、Consul 的外部键及其他 `json_name` 不变。此次只拆分已确认的 ID 词边界，不调整既有缩写、业务词汇或拼写。

访客房间的四个枚举符号由 `ACTION_UNSPECIFIED`、`ACTION_DAMAGE`、`ACTION_SKILL`、`ACTION_REST` 改为 `action_unspecified`、`action_damage`、`action_skill`、`action_rest`，数值仍依次为 0、1、2、3。更新 C++ 常量引用，并从相同 schema 重新生成 C#。Protobuf 二进制帧及共享线格式向量不变；使用 Protobuf JSON／文本格式的工具须采用新的枚举字符串，这部分文本名称不兼容旧大写形式。

`check-proto-naming` 通过真实 protoc 为 `src/proto/proto/` 与 `resources/photon/proto/` 生成 descriptor set，再检查所有源码文件、手写消息、字段、oneof、枚举及枚举值的 lower_snake_case。检查排除 protoc 自动生成的 map entry 类名及 proto3 optional 合成 oneof，并验证改名字段的 tag／类型／JSON 键及代表性旧二进制、JSON 消息。该目标随 `check-core` 执行；也可独立运行：

```sh
cmake --build app-build --target check-proto-naming --parallel 1
```

## 外部 JSON 拼写

Consul 的 C++ 字段使用小写，`json_name` 显式保留其外部键。例如 `consul::service_data::meta()` 对应 JSON 的 `Meta`；`ID`、`Name`、`Address`、`Port`、`Tags`、`Check` 也保留要求的拼写。不要为匹配 C++ 标识符而修改这些外部键。

协议约定与代表性二进制、JSON 回归见 [consul.proto](https://github.com/OpenArkStudio/ark/blob/main/src/proto/proto/consul.proto) 和 [protobuf_messages.cpp](https://github.com/OpenArkStudio/ark/blob/main/tests/core/protobuf_messages.cpp)。文本 descriptor 变化与二进制兼容是不同问题，本次迁移不保证全面的源码兼容或 JSON 枚举字符串兼容。
