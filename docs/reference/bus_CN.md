# Bus 插件

Bus 为进程分配逻辑地址，读取应用类型、进程端点和连接关系。`bus_module` 提供配置查询，`msg_module` 根据应用类型、Bus ID 或 Session 发送消息；实际连接由 Net 插件管理。

## API 与私有创建

`bus_module.hpp` 与 `msg_module.hpp` 和实现共置于 `src/plugins/bus/`。
CMake 通过显式映射，由 `ark::bus` 保留 `<ark/bus/bus_module.hpp>` 与
`<ark/bus/msg_module.hpp>` 路径。消息接口需要 `ARK_BUILD_PROTO=ON`；API 目标会传递
Net 与协议头依赖。共享地址和配置类型 `<ark/bus.hpp>` 仍位于 `include/ark/`，供核心与 Net 使用。

两个具体模块定义均位于 cpp，继承 `module_base<接口>`。私有工厂返回公开接口，
`plugin_base` 先注册 Bus 接口，再注册消息接口，并按逆序卸载。两个模块均不注册更新回调，
与原有继承的默认更新行为一致。公开虚接口、RTTI 查找键、`ark_bus_plugin` 库名与
`DllEntryPlugin`／`DllExitPlugin` 签名保持不变；Registry、IO 所有权与网络行为维持既有边界。

**头文件迁移：**移除 `<ark/bus/bus_module_impl.hpp>`、`<ark/bus/msg_module_impl.hpp>` 与
`<ark/bus/bus_plugin.hpp>` 的直接包含。加载运行时插件后，使用
`manager.find_module<ark::bus_module>()` 或 `manager.find_module<ark::msg_module>()`。
改用目标传递的公开头文件包含目录，不能仅依赖 `-I <源码>/include`。私有解析与装配头不进入公开映射。

清单中的配置路径保持不变，仍相对于选定的插件配置目录解析：

```toml
[[plugins]]
name = "ark_bus_plugin"
config = "bus.plugin.toml"
```

应用还需按原有模块依赖加载 Net 与 Utility。Net 在 `post_init()` 中加入配置指定的 SS 出站连接，
应用仍须显式启动监听。公开值类型和消息序列化接口保持不变。

启用应用构建时，`check-core` 包含的 `check-bus-plugin` 加载真实 Bus／Net／Utility 动态库，
验证两个公开接口键、TOML 查询、通过记录型 Net 连接传递的 Protobuf 消息头与正文、发送失败及正常重复停服。
既有 Bus 配置测试通过私有接口工厂保留全部断言，也覆盖不启用 Protobuf 的配置构建。
`tests/consumer/bus` 仅链接 `ark::bus`，分别编译两个公开模块头，并拒绝私有实现／配置头。
消费者命令见[目录结构](directory-structure_CN.md)。

## 地址与职责

Bus 地址使用 `channel.zone.app.instance` 四段形式，例如 `1.1.31.1`。四段各占 8 位，组合为 `bus_id_t`；它是进程标识，不是 IP 地址或监听端口。

| 段 | 含义 | 示例 |
| --- | --- | --- |
| `channel` | 应用划分的渠道 | `1` |
| `zone` | 应用划分的区域 | `1` |
| `app` | `app_type` 的数值 | `31` 对应 `ark_app_game` |
| `instance` | 同类进程实例编号 | `1` |

| 接口 | 职责 |
| --- | --- |
| `bus_module` | 本进程信息、应用名称映射、目标关系和其他进程配置 |
| `msg_module` | 消息序列化、选择已有连接、填写消息头并请求发送 |
| `net_service_manager_module` | 监听服务、客户端连接、Bus 与 Session 的映射 |
| `net_client_service` / `net_server_service` | 收包回调、连接事件和连接状态处理 |

## 配置进程与连接关系

`bus_module::init()` 读取 `bus.plugin.toml` 的四个部分：

| 配置节点 | 内容 |
| --- | --- |
| `bus.applications` | 应用名称与 `app_type` 数值的映射 |
| `bus.relations` | 有方向的连接目标：`source` 应连接到哪些 `target` 类型 |
| `bus.processes` | 每个 Bus ID 的监听端点、连接上限和线程配置 |
| `bus.registry` | 注册中心参数，供 `get_reg_center()` 查询 |

下面说明 Game 与 World 两类进程的配置关系。内外端点使用不同端口，避免同时监听时冲突。

```toml
[bus]
[[bus.applications]]
name = "world"
type = 30
[[bus.applications]]
name = "game"
type = 31
[[bus.relations]]
source = "game"
target = "world"
[[bus.processes]]
bus_id = "1.1.30.1"
server_endpoint = "tcp://127.0.0.1:9101"
intranet_endpoint = "tcp://127.0.0.1:9111"
max_connections = 100
threads = 1
[[bus.processes]]
bus_id = "1.1.31.1"
server_endpoint = "tcp://127.0.0.1:9102"
intranet_endpoint = "tcp://127.0.0.1:9112"
max_connections = 100
threads = 1
[bus.registry]
host = "127.0.0.1"
port = 8500
service_name = "ark-service"
check_interval = "10s"
check_timeout = "3s"
```

应用加载 Bus、Net 和 Utility，设置部署分配的 Bus ID，并通过 `create_server()` 启动监听。Net 在 `post_init()` 中按有向关系选择目标类型，并仅连接与本进程渠道和区域相同的静态进程。排除自身后，每个目标 Bus ID 只加入一次既有 SS 客户端服务。关系没有匹配的进程记录时，不加入连接。Bus 查询语义不变；`get_other_proc(type, list)` 本身不筛选渠道或区域。

选中目标的 `intranet_endpoint` 必须包含可拨号的 TCP 或 UDP 主机地址、非零端口，且不能带路径。端点缺失、传输协议不受支持或拨号地址为通配地址（`0.0.0.0`、`::`）时，Net 启动失败，诊断包含目标 Bus ID 和字段。支持主机名。监听仍可使用通配地址：需要通配绑定时，用 `server_endpoint` 监听，并为 SS 流量配置可达的 `intranet_endpoint`。目前 `intranet_endpoint` 同时用于 SS 监听和对外提供的拨号地址，没有独立的绑定地址与通告地址字段。

连接请求在后续管理器更新中处理；启动成功不代表目标已经连接。目标可以稍后启动，也可以沿用相同身份和端点重启。连接生命周期继续使用既有客户端状态机、SS 注册和心跳。配置是启动时的快照，不支持热重载、动态成员管理、自动分配 ID 或全局唯一性校验。包括 Kubernetes 在内的部署环境须分配稳定的 Bus ID 和可达端点；这不表示已接入 Kubernetes API。

`bus.registry` 有配置项不等于已启用动态注册或发现。当前网络服务管理模块中的 Consul 注册 / 健康检查发现路径仍为注释，不能依靠这份配置自动获得目标进程。

## 迁移 XML 运行设置

Bus 运行设置现在要求 TOML 内容，不能只修改旧 XML 文件的扩展名。将其替换为 `bus.plugin.toml`，并更新各应用清单中 `ark_bus_plugin` 的配置引用。源码配置位于 `config/bus.plugin.toml`，部署后仍使用运行目录的 `conf/` 前缀。应用清单仍支持 XML，业务数据 XML 保持不变。

| XML 位置或属性 | TOML 位置或键 |
| --- | --- |
| `applications/application` | `[[bus.applications]]`；`name`、`type` 不变 |
| `relations/relation` | `[[bus.relations]]`；`source`、`target` 不变 |
| `processes/proc` | `[[bus.processes]]` |
| `busid` | `bus_id` |
| `endpoint_server`、`endpoint_intranet` | `server_endpoint`、`intranet_endpoint` |
| `max_connection`、`thread_num` | `max_connections`、`threads` |
| `center` / `ip` | `[bus.registry]` / `host`；其余键不变 |

四个部分及其字段均为必填。数组可以为空：在 `[bus]` 中写入 `applications = []`、`relations = []`、`processes = []`。未知字段、TOML 类型错误或非法值会导致初始化失败；诊断包含文件、部分或数组下标以及字段。数值必须使用 TOML 整数：应用类型为 `1..254`，连接数上限为 `0..4294967295`，线程数为 `0..255`，注册中心端口为 `0..65535`。零值保留原有配置含义；解析器不启动服务，也不决定并发策略。

Bus ID 必须恰好包含四个 `0..255` 的十进制分量。非空端点必须使用受支持的协议和 `protocol://host:port` 格式（可附加路径），端口为 `0..65535`。主机名原样保留，不进行 DNS 查询。IPv6 字面量必须加方括号，例如 `tcp://[::1]:9011`；解析器仅在本地校验其数值语法，保存供 arknet 使用的无方括号主机名 `::1`。公共端点格式化方法保持不变。端点仍可为空，表示禁用对应监听；两个端点分别校验。注册中心的检查间隔和超时字符串原样传递，不解释时长语法。

公共 `app_config`、`proc_config`、`reg_center` 字段及查询方法不变。应用名称、应用类型、有向关系或进程 Bus ID 重复时，Bus 初始化失败，诊断包含文件、数组下标和字段。本进程未配置时仍保持未配置状态。私有结构校验使用 `ark::config` 编译目标提供的共享文件／TOML 读取器；toml++ 仅由配置和插件实现目标内部使用，不进入公开接口。

**升级：**启动 Net 前，删除重复记录，并为出站关系选中的同渠道、同区域进程提供可达的 `intranet_endpoint`。此前可加载的重复记录或选中目标的空端点现在会导致启动失败。关系现在会自动发起出站连接；不应发起流量的关系须删除。随仓库提供的通用配置含历史记录，不是完整的部署清单；应按所选进程身份核对关系及端点。

## 查询本进程与目标进程

在模块完成初始化后，通过插件管理器取得 Bus 接口。以下函数收集当前应用类型对应的、静态配置中的目标进程。

```cpp
#include <vector>

#include "ark/bus/bus_module.hpp"
#include "ark/plugin_manager.hpp"

std::vector<ark::proc_config> configured_targets(ark::plugin_manager& manager) {
    auto* bus = manager.find_module<ark::bus_module>();
    if (bus == nullptr)
    {
        return {};
    }

    std::vector<ark::app_type> types;
    if (!bus->get_target_bus_relations(types))
    {
        return {};
    }

    std::vector<ark::proc_config> result;
    for (auto type : types)
    {
        std::vector<const ark::proc_config*> processes;
        bus->get_other_proc(type, processes);
        for (const auto* process : processes)
        {
            result.push_back(*process);
        }
    }
    return result;
}
```

`get_other_proc(type, list)` 向输出列表追加配置指针，返回追加后列表的总长度。上例每次使用空列表，并复制配置，避免把模块内部指针保存在外部。该查询按类型筛选，不额外限定渠道或区域；应用需要时应自行筛选。

| 查询 | 结果与限制 |
| --- | --- |
| `get_self_bus_id()` / `get_self_bus_name()` | 当前进程的整数 ID / 四段地址 |
| `get_self_proc()` | 本进程的 `proc_config`；它是静态配置，不代表监听已启动 |
| `get_app_type(name)` | 未找到时返回 `ark_app_default` |
| `get_app_name(type)` | 未找到时返回空字符串 |
| `get_other_proc(id, proc)` | 找到时复制配置并返回 `true`；未找到返回 `false` |
| `combine_bus_id(type, instance)` | 沿用本进程的渠道和区域，组合指定类型与实例的 ID |

## 显式 TCP 连接

目标进程通过 `net_service_manager_module::create_server()` 启动监听。关系选中的常规目标会自动加入连接；对于应用在该启动策略之外自行管理的目标，可通过以下既有接口显式加入 TCP 连接。

```cpp
#include "ark/bus/bus_module.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/plugin_manager.hpp"

bool queue_tcp_connection(ark::plugin_manager& manager, ark::bus_id_t target_id) {
    auto* bus = manager.find_module<ark::bus_module>();
    auto* network = manager.find_module<ark::net_service_manager_module>();
    if (bus == nullptr || network == nullptr)
    {
        return false;
    }

    ark::proc_config target;
    if (!bus->get_other_proc(target_id, target))
    {
        return false;
    }

    const auto& endpoint = target.intranet_ep;
    if (endpoint.proto() != ark::proto_type::tcp || endpoint.get_ip().empty() || endpoint.get_port() == 0)
    {
        return false;
    }

    return network->create_client_service(
        ark::bus_addr(target_id), ark::proto_type::tcp, endpoint.get_ip(), endpoint.get_port());
}
```

返回 `true` 表示连接请求已加入客户端服务，连接在后续 `update()` 中处理；它不表示已经连接成功。通过客户端服务的 `reg_net_event_callback()` 观察连接事件，通过 `reg_msg_callback()` 注册消息处理。接收端使用对应服务端接口注册回调。

当前客户端服务按状态处理断线重连，重试间隔写在实现中，为 30 秒；它不是此配置文件中的可调退避策略。重连不会替应用重放此前发送失败的业务消息。

## 发送与接收消息

连接可用后，通过 `msg_module` 发送应用自己的 Protobuf 消息。下面的函数接收调用方定义的消息 ID 和消息类型，不预设业务协议。

```cpp
#include "ark/bus/msg_module.hpp"

bool send_to_process(ark::msg_module& messages, ark::bus_id_t target_id, ark::msg_id_t message_id,
    const google::protobuf::Message& body) {
    return messages.send_msg_by_bus_id(target_id, message_id, body);
}
```

| 发送接口 | 连接选择 |
| --- | --- |
| `send_msg_by_bus_id()` | 使用源进程与目标 Bus ID 对应的已有连接，生成进程间消息头 |
| `send_def_ss_msg_by_app_type()` | 在指定应用类型的客户端服务中，通过一致性哈希选择连接；当前选择键为本进程 Bus ID |
| `send_def_cs_msg_by_app_type()` | 与上项采用相同的连接选择方式，使用客户端消息头 |
| `send_to_client()` | 通过外部监听服务，发送到指定 Session |
| `send_to_intra()` | 通过内网监听服务，发送到指定 Session |

进程间消息头携带消息 ID、源 / 目标 Bus ID 和 Actor ID。发送失败会返回 `false`，例如没有服务、连接不可用或序列化失败；返回 `true` 不表示远端业务已处理成功。需要请求响应、超时和幂等保证时，由应用协议实现。

接收流程是：网络服务拆出 `net_msg` → 按消息 ID 调用已注册回调 → 使用 `msg_module::decode_protobuf()` 解析业务消息。解析函数返回 `false` 表示 Protobuf 解析失败；`read_message_body()` 可复制原始消息体并取得 Actor ID。回调中的 `net_msg*` 是借用数据，需要延后处理时应先复制所需内容。

## 覆盖范围与后续工作

`check-static-services`（Unix，启用应用构建时纳入 `check-core`）在私有回环 TCP 端点上启动两个独立持有的进程，加载真实 Bus／Net／Utility 动态库。测试覆盖类型／渠道／区域筛选与自身排除、选中目标的端点诊断、SS 注册与 Session 映射、Protobuf 请求响应、断连期间发送失败，以及目标重启后按既有 30 秒服务重试策略完成第二次往返。两个进程正常停服，控制进程将原始子进程日志保留在本地。启用应用和核心测试后，运行 `cmake --build build --target check-static-services --parallel 1`。

这是有界的本地功能验收，不构成跨机器路由、UDP 集成、Windows 进程控制器、容量或部署保证。底层传输能力见 [Net 插件](network_CN.md)。TODO：提供独立部署示例；跨机器路由、动态注册／发现另行实现和验证。

## API 来源

- [Bus 地址与进程配置](https://github.com/OpenArkStudio/ark/blob/main/src/core/bus.hpp)
- [Bus 接口](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/bus/bus_module.hpp)
- [消息接口](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/bus/msg_module.hpp)
- [网络服务管理接口](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/net/net_service_manager_module.hpp)
- [网络构建与测试](../networking_CN.md)
