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

| 链接／部署边界 | 要求 |
| --- | --- |
| Windows 消息库 | `AFProto`／`ark::proto` 是单一共享生成库；宿主与插件共用消息类／描述符，包括宿主为公开 Kernel 转换创建的对象 |
| 生成声明 | protoc 生成 `ARK_PROTO_EXPORT`，`ark::proto` 传播导入定义；不要把另一份 `.pb.cc` 编入消费者或手工定义导出宏 |
| 其他平台 | 保留静态 AFProto 与原生成命令 |
| **Windows 二进制迁移** | 同时重建宿主／插件，使用同一套兼容 Protobuf 运行时及构建配置；部署构建 `bin/` 的 `AFProto.dll`（Debug：`AFProto_d.dll`） |
| 安装／兼容 | 既有 RUNTIME 规则将 DLL 装入 `bin/`，导入库装入库目录；即使 `.lib` 同名，也不混用旧静态归档与新导入库；字段号／二进制编码不变 |
| 插件依赖 | 加载器先解析本机绝对路径，从插件旁查找 `AFProto_d.dll` 等依赖，不受程序／工作目录位置影响；依赖应随插件部署，缺失／不兼容仍失败并保留 Windows 原生错误码 |

| 检查 | 证据范围 |
| --- | --- |
| `check-proto-configuration` | 两个 CMake 分支与真实 protoc：消息／描述符导出、消费者导入、非 Windows 生成路径；不跨平台编译／加载 Windows 二进制 |
| `check-kernel-plugin` | 宿主创建 Protobuf 对象并真实跨 DSO 转换 |
| `check-native-loader` | 宿主／插件分目录、依赖仅在插件旁，覆盖绝对／相对路径；Windows 加载验收须原生运行 |

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

访客房间枚举仅改符号，数值保留：

| 原符号 | 新符号 | 数值 |
| --- | --- | ---: |
| `ACTION_UNSPECIFIED` | `action_unspecified` | 0 |
| `ACTION_DAMAGE` | `action_damage` | 1 |
| `ACTION_SKILL` | `action_skill` | 2 |
| `ACTION_REST` | `action_rest` | 3 |

更新 C++ 常量并从同一 schema 重新生成 C#。二进制帧与共享线格式向量不变；JSON／文本工具须采用新枚举字符串，与旧大写文本不兼容。

`check-proto-naming` 通过真实 protoc 为 `src/proto/proto/` 与 `resources/photon/proto/` 生成 descriptor set，再检查所有源码文件、手写消息、字段、oneof、枚举及枚举值的 lower_snake_case。检查排除 protoc 自动生成的 map entry 类名及 proto3 optional 合成 oneof，并验证改名字段的 tag／类型／JSON 键及代表性旧二进制、JSON 消息。该目标随 `check-core` 执行；也可独立运行：

```sh
cmake --build app-build --target check-proto-naming --parallel 1
```

## 外部 JSON 拼写

Consul 的 C++ 字段使用小写，`json_name` 显式保留其外部键。例如 `consul::service_data::meta()` 对应 JSON 的 `Meta`；`ID`、`Name`、`Address`、`Port`、`Tags`、`Check` 也保留要求的拼写。不要为匹配 C++ 标识符而修改这些外部键。

协议约定与代表性二进制、JSON 回归见 [consul.proto](https://github.com/OpenArkStudio/ark/blob/main/src/proto/proto/consul.proto) 和 [protobuf_messages.cpp](https://github.com/OpenArkStudio/ark/blob/main/tests/core/protobuf_messages.cpp)。文本 descriptor 变化与二进制兼容是不同问题，本次迁移不保证全面的源码兼容或 JSON 枚举字符串兼容。
