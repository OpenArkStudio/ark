# Net 插件

网络插件将独立的 arknet 网络库接入 ark，处理业务拆包、Session ID 与 Update 线程上的业务回调。

| 传输 | 客户端 | 服务端 |
| --- | --- | --- |
| TCP | `tcp_client` | `tcp_server` |
| WebSocket | `websocket_client` | `websocket_server` |
| RUDP | `rudp_client` | `rudp_server` |
| HTTP | `http_client_module` | `http_server` |

`net` 定义启动、关闭、发送和 Update 操作。业务消息继续使用现有消息头；RUDP 使用 arknet 兼容的 ARQ 报文格式，默认关闭 FEC。

构建命令和验证范围见[网络构建与测试](../networking_CN.md)。接口见 [ark::net](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/net/net.hpp)，协议能力见 [arknet 指南](https://openarkstudio.github.io/arknet/zh/features/)。

可选的[HTTP 服务端插件](http_CN.md)公开 `http_server_module`，其路由处理器在 arknet IO 工作线程上运行。

## API 与私有创建

Net 的全部源头文件与实现位于 `src/plugins/net/`。`ark::net` API 目标通过显式清单，
将 18 个公开头映射到 `<ark/net/...>`，并传递公开依赖。包含协议消息的服务头需要
`ARK_BUILD_PROTO=ON`；此时 `ark::net` 会传递 `ark::proto`。直接端点头仍可在关闭协议生成时使用。
API 目标本身不会构建或加载运行时动态库。

私有插件采用 `plugin_base`，工厂返回 `std::unique_ptr<net_service_manager_module>`，
具体类型在 cpp 中继承 `module_base<net_service_manager_module>`。查找继续使用公开接口的
RTTI 键。另一个私有工厂返回 `std::unique_ptr<http_client_module>`，具体类型仅在
`http_client_module_impl.cpp` 内声明，继承 `module_base<http_client_module>`。
HTTP 客户端仍按其公开接口键注册。两个模块仍参与更新；HTTP 先安装、最后卸载。
库名 `ark_net_plugin` 与 `DllEntryPlugin`／`DllExitPlugin` 签名保持不变，arknet 继续拥有
IO 工作线程与传输操作。

**头文件迁移：**移除 `<ark/net/net_service_manager_module_impl.hpp>`、
`<ark/net/net_plugin.hpp>` 和 `<ark/net/http_client_module_impl.hpp>` 的包含。通过应用清单加载插件，再获取已注册的
`net_service_manager_module`，不直接构造管理器实现。改用 CMake 目标传递的包含目录，
不能仅使用 `-I <源码>/include`。安装后的消费者使用 `find_package(ark CONFIG REQUIRED COMPONENTS net)` 和 `ark::net`；独立 arknet 依赖及设置一致性要求见[安装说明](../get-started/installation_CN.md#安装并消费-cmake-包)。

**源码／ABI 不兼容变更：** `http_client_module_impl` 及其旧公开头不再属于源码、构建或安装接口。
包含 `<ark/net/http_client_module.hpp>`，通过插件管理器查找 `http_client_module`，
不直接构造具体客户端。工厂属于 Net 内部实现，不是应用 API；Net 与消费者需要一起重建。
安装时使用新前缀，避免旧安装遗留已移除的头文件。
`net_client_service_impl.hpp`、`net_server_service_impl.hpp`、端点类与回调模板继续公开。
请求重载、超时默认值、future 结果、所属 update 线程的回调派发及停机行为保持不变。

## 服务端与客户端服务示例

`msg_header::max_body_size() const` 定义本地协议配置允许的正文长度上限，
原生 CS／SS 头部的默认值仍为 4096 字节。自定义头部可以覆写该方法，
并须在 `create()` 和 `clone()` 中保留同一策略。收包解析在消息分配之前检查配置原型的上限；
解码后的线格式头部不能协商扩大上限。TCP、WebSocket 和 RUDP 的发送与广播检查传入头部的上限，
并防止总长度计算溢出。收包缓冲的初始容量仍与消息上限分别配置。

**升级：**新增虚方法改变了 `msg_header` 的虚表 ABI。须一起重新构建 Net、协议头部实现和全部消费者／插件，
不能混用新旧二进制。原生 CS／SS 线格式及其默认 4096 字节上限不变。
自定义协议两端的上限匹配由应用负责。

进程需加载 Bus、Utility 和 Net，并提供自身的 Bus 配置。启动后显式创建监听；
加载插件不会自动监听。两种角色均通过公开管理器接口使用：

```cpp
#include <ark/net/net_service_manager_module.hpp>
#include <ark/plugin_manager.hpp>

bool start_listeners(ark::plugin_manager& manager) {
    return manager.find_module<ark::net_service_manager_module>()->create_server();
}

bool connect_tcp(ark::plugin_manager& manager, const ark::bus_addr& target,
    const std::string& host, uint16_t port) {
    return manager.find_module<ark::net_service_manager_module>()->create_client_service(
        target, ark::proto_type::tcp, host, port);
}
```

连接请求保持既有异步语义。继续在所属线程调用 `manager.update(timestamp)`，
并在回调停止前保持接收者有效。Net 也会在启动时根据 Bus 关系加入 SS 出站连接，
仅选择同渠道、同区域的指定目标类型。应用仍须显式启动监听。
端点校验、迁移要求和有界双进程重启测试见 [Bus](bus_CN.md)。

启用应用构建时，`check-core` 包含的 `check-net-plugin` 加载真实 Bus／Utility／Net 动态库，
验证两个接口键、管理器线程上的 TCP 消息与 HTTP 回调、无需管理器更新即可完成的 future、
`pre_shut()` 对未完成请求的取消、正常重复停服及监听释放。DSO 创建的 future 在实现库
仍加载时消费，不代表 future 可以跨任意动态库卸载继续使用。
独立的 `tests/consumer/net` 工程仅链接 `ark::net`，分别编译各公开头，
并拒绝已移除实现及私有工厂／装配头路径。既有 `check-network` 继续覆盖端点、拆包、重连与停服。
使用 Boost 时，`check-install` 还通过迁移后的 `ark::net` 安装包编译公开 HTTP 客户端接口，
并拒绝安装目录中出现实现头文件。

## 应用层初始收包缓冲

TCP、WebSocket 与 RUDP 端点使用 4 KiB 初始应用层收包缓冲，随后按需增长。在 `start_client` 或 `start_server` 前，通过网络对象的 `set_initial_receive_buffer_size(bytes)` 配置；`get_initial_receive_buffer_size()` 读取配置值。传入零，或端点运行时修改，会返回 `false`；既有 Session 保留创建时的容量。

这是应用消息的拆包缓冲，与内核 socket 收发缓冲分别配置，也不是 4 KiB 的最大消息限制。[本地收包缓冲报告](../performance/receive-buffers_CN.md)保留了内存申请、分片输入与大批量吞吐量之间的取舍，不把这些结果视为网络 RTT 或连接容量。

## 收包缓冲的所有权

**源码／布局不兼容变更：** `<ark/buffer.hpp>` 提供的 `ark::buffer` 仅支持移动。复制构造和复制赋值被显式删除，避免两个对象重复释放同一数组。Core／Net 消费者及其插件必须一起重建。仓库中的 Net Session 原本就禁止复制，并在对象内部持有缓冲；复制 `shared_ptr` Session 句柄不会复制该缓冲。这次仓库调用检查不能证明外部消费者从未使用原有隐式复制操作。

移动构造和移动赋值均为 `noexcept`，转移数组、未读数据、容量和游标状态。移动赋值释放目标原有数组；自身移动保持原状态。移出对象的容量和未读长度均为零，数据指针为空，可继续销毁、赋值或写入。原有公开方法签名继续保留。独立构造默认容量仍为 512 KiB，Net Session 继续使用独立配置的 4 KiB 默认容量。

```cpp
ark::buffer pending(4096);
pending.write("abc", 3);
ark::buffer processing(std::move(pending)); // 转移所有权
pending.write("next", 4);                 // 重新申请存储
```

使用 `std::move` 时包含 `<utility>`。`get_data()` 返回当前数组中的借用指针；所属对象必须保持有效，在压缩、扩容或替换所有权后重新获取指针。本次不增加 clone API。需要独立数据的应用可在源数据有效期间，将可读字节显式写入另一个缓冲。

缓冲通过 `ARK_NEW_ARRAY`／`ARK_DELETE_ARRAY` 保留最小分配兼容边界，构造、扩容和释放现在使用同一对钩子。这修正了原扩容路径通过裸 `new[]`／`delete[]` 绕过自定义钩子的问题。覆盖钩子时，须在包含头文件前定义匹配且释放时不抛异常的实现，并确保各编译单元定义一致。默认分配仍使用会抛异常的 `new[]`：`std::bad_alloc` 继续向外传播，扩容失败保留原有数据、容量和游标。自定义分配器返回空指针时，构造得到容量为零的空缓冲；`write()` 返回 `false`，不改变已有状态。

零长度写入成功且不解引用传入指针；非零长度的空源指针返回 `false`。移除数量超过未读长度时不改变状态，包括会使加法溢出的数量。容量无法表示时，在分配前抛出 `std::length_error`。正常读写、压缩和容量增长行为保持不变。

开启 `ARK_BUILD_CORE_TESTS=ON` 后，`check-buffer` 运行生命周期／移动、压缩／扩容、边界、默认分配确定性失败及独立自定义钩子回归，并纳入 `check-core`。分配失败测试只使一次小数组申请失败，不申请巨量内存。`check-network` 保留既有 Net 拆包与分片输入覆盖。这些检查不代表服务或容量验收。

## 消息与事件所有权

`net_msg` 拥有包头和消息体。存储容量与声明的消息体长度独立；回调指针仍为借用指针。

| API | 行为与失败处理 |
| --- | --- |
| `net_msg::alloc_msg(head)` | 始终接管堆分配包头。包头为空或对象、消息体分配失败时返回空指针，使用前须检查。接收拆包在包头或消息分配失败时返回 `false`，不将不完整消息入队。 |
| 原生 CS/SS `create()` / `clone()`、`net_event::alloc_event()` | 分配失败返回空指针。 |
| 消息/事件 `release(pointer)` | 销毁对象并将调用方指针置空；允许空指针。 |
| `copy_data(data, length)` | 输入须有 `length` 个可读字节，支持重叠复制；先扩容再修改长度。零长度允许空指针并清零长度；非零空输入抛出 `std::invalid_argument`。 |
| `set_msg_length(length)` | 按需扩容并保留原有前缀；新增字节未初始化，须初始化后使用。 |
| `copy_from(source)` | 深复制包头和消息体，自复制不变。空来源抛出 `std::invalid_argument`；空克隆抛出 `std::bad_alloc`；自定义 `clone()` 异常向上传播。来源声明长度超过容量时抛出 `std::length_error`。 |
| `get_head()` / `get_msg_data()` | 直接修改包头长度不调整存储，应使用 `set_msg_length()` 或 `copy_data()`。消息体指针为借用指针：保持所属对象有效，扩容或复制替换后重新获取，释放后丢弃。 |

扩容/复制分配失败抛出 `std::bad_alloc`；分配及克隆失败均保留目标原有包头、字节和长度。

**升级要求：**容量记录改变 `net_msg` 布局，须同时重编译 Net、消费方及插件。这些消息、事件和原生包头工厂使用标准分配，`ARK_NEW` / `ARK_DELETE` 覆盖不再定制它们。公开签名、CS/SS 报文字节和平台类型不变。

启用 `ARK_BUILD_NETWORK_TESTS=ON` 后，`check-net-message` 无需套接字即可验证所有权、拆包和确定性分配失败；`check-network` 包含该目标。本地检查不代表共享服务或容量验收。

## 成员回调与借用参数

端点构造函数与客户端／服务端服务的注册接口通过 C++20 `std::bind_front`，
将成员函数绑定到传入的同一个非拥有接收者指针。注册不会调用回调或延长接收者生命周期；
在回调不再可能执行前，接收者必须保持有效。公开回调签名与 Update 线程派发保持不变。

消息与事件指针在本次调用期间借用原对象。WebSocket 客户端的 `const conv_id_t&`
也引用本次调用的参数；需要保留时复制其值，不保留引用。服务端服务的转发回调仍使用
原有 `int` 参数及从 `conv_id_t` 的转换，本次改动不扩大该参数的取值范围。

启用 `ARK_BUILD_PROTO=ON` 的 `check-core` 验证公开服务注册助手，其头文件原本就依赖
Protobuf。`check-network` 验证真实端点派发，包括 WebSocket 的引用参数与所属 Update 线程。
这些检查不构成容量验收，也不改变所有权模型。
