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

Windows 插件加载器会先把请求路径解析为本机格式的绝对路径。即使可执行文件或工作目录位于其他位置，也会从插件目录查找旁边的依赖，例如 `AFProto_d.dll`。部署时将依赖与插件放在一起；缺失或不兼容的库仍会导致加载失败，并保留 Windows 原生错误码。

`check-proto-configuration` 配置两个 CMake 分支并运行真实 protoc，检查消息／描述符导出声明、消费者导入定义及原有非 Windows 生成路径。它不在其他平台上编译或加载 Windows 二进制。`check-kernel-plugin` 保留宿主创建的 Protobuf 对象及真实跨 DSO 转换。`check-native-loader` 单独测试宿主与插件分目录、依赖只放在插件旁边的场景，同时覆盖绝对和相对路径。Windows 加载验收仍要求该平台的原生运行。

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

## 外部 JSON 拼写

Consul 的 C++ 字段使用小写，`json_name` 显式保留其外部键。例如 `consul::service_data::meta()` 对应 JSON 的 `Meta`；`ID`、`Name`、`Address`、`Port`、`Tags`、`Check` 也保留要求的拼写。不要为匹配 C++ 标识符而修改这些外部键。

协议约定与代表性二进制、JSON 回归见 [consul.proto](https://github.com/OpenArkStudio/ark/blob/main/src/proto/proto/consul.proto) 和 [protobuf_messages.cpp](https://github.com/OpenArkStudio/ark/blob/main/tests/core/protobuf_messages.cpp)。文本 descriptor 变化与二进制兼容是不同问题，本次迁移不保证全面的源码兼容或 JSON 枚举字符串兼容。
