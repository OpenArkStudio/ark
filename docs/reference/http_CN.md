# HTTP 服务端插件

可选 HTTP 插件公开 `http_server_module` 与 arknet 的 `http_router` 类型。
它显式启动 HTTP/1 监听，并在 arknet IO 工作线程上路由请求；Net 插件的 HTTP 客户端仍是独立接口。

## 构建与配置

启用 `ARK_BUILD_APPLICATIONS=ON`、`ARK_BUILD_PLUGIN_HTTP=ON`；应用使用 Boost.Asio 与 Boost.Beast。
部署带平台后缀及 Debug `_d` 的 `ark_http_plugin`，在应用清单中选择插件与配置：

```toml
[[plugins]]
name = "ark_http_plugin"
config = "http.plugin.toml"
```

配置路径相对于选定的插件配置目录解析。源码 `config/http.plugin.toml` 内容为：

```toml
[http]
host = "0.0.0.0"
port = 9090
threads = 4
```

三个字段均必需。`host` 是不含 NUL 的非空字符串，`port` 是 `0..65535` 的 TOML 整数，
`threads` 是 `1..255` 的整数。未知键、缺失／错误类型和越界值会使 `init()` 失败，诊断包含文件与字段；
语法错误还包含行列。给旧 XML 文件改名不会转换内容。参见[插件配置](../service-manifests_CN.md#http-与-consul-插件配置)。

`start_server()` 使用配置值。显式重载保留公开默认参数 `port = 8080`、`thread_num = 1`，
它们不是 TOML 默认值。零端口请求操作系统分配可用端口；公开模块不提供所选端口查询，
客户端需要发现监听地址时应选择已知配置端口。初始化只解析配置，不启动监听。

## 路由请求

启动后获取必需的已注册接口，在启动监听前注册路由：

```cpp
#include <ark/http/http_server_module.hpp>
#include <ark/plugin_manager.hpp>

bool listen(ark::plugin_manager& manager) {
    auto* server = manager.find_module<ark::http_server_module>();
    server->get_router().add(arknet::http::verb::get, "/users/:id",
        [](const auto& context, auto& response) {
            response.set(arknet::http::field::content_type, "text/plain");
            response.body() = "user:" + std::string(context.param("id"));
        });
    return server->start_server();
}
```

路由引用从模块借用；`get_router()` 每次返回同一个自有路由器。路由上下文与响应参数仅属于本次调用。
保持处理器状态有效，并同步 IO 工作线程访问的共享业务状态。路由不等待 `manager.update()`，
也不把处理器移到管理器线程。`make_200<response_body_type>()` 保留公开响应助手，
包括内容类型、长度和 keep-alive 行为。

运行中的模块拒绝再次启动。同一库中的两个模块实例不能预留相同非零端点。
`pre_shut()` 停止并释放服务端和端点预留，随后可再次启动该端点。正常管理器停服由所属线程驱动。
显式调用 `stop_accepting()` 后，本实例不再运行新请求的业务路由，而是返回 HTTP 503 并关闭该连接。已进入路由的请求继续执行；`ready_to_shut()` 等待处理器及其响应发送完成，再允许正常停机释放监听器。此阶段按业务请求接纳，不关闭 TCP 监听器；持续连接和新连接都受同一检查。响应发送失败也会结束该请求的等待，不等于客户端已收到响应。

停止接收是本实例的永久状态，不能通过再次 `start_server()` 重新开启；仅直接 `pre_shut()` 的既有重启用法保持不变。管理器仍使用原有协作式等待和超时保留规则，不能中断阻塞的路由。处理器自己投递的异步工作须另行托管，不能靠 HTTP 响应发送计数推断完成。这不提供通用插件热替换或 HTTP/2。arknet 继续拥有 IO 工作线程、请求解析与传输操作。

## 公共插件接口与头文件映射

公开头位于 `src/plugins/http/http_server_module.hpp`。`ark::http` 通过显式 CMake 映射，
保留 `<ark/http/http_server_module.hpp>`，关闭运行时插件与 Protobuf 后仍可使用。
具体类在 cpp 中继承 `module_base<http_server_module>`，由私有接口工厂创建；私有插件装配采用
`plugin_base` 及显式 `DllEntryPlugin`／`DllExitPlugin`。公开虚接口与路由类型、RTTI 键、库名和入口签名保持不变。
模块不注册更新回调，与原有继承的默认更新行为一致。

**头文件迁移：**将 `<ark/http/http_server_module_impl.hpp>` 与 `<ark/http/http_plugin.hpp>`
改为公开模块头，通过 `plugin_manager` 获取已加载接口。使用 `ark::http` 传递的包含目录，
不能仅依赖 `-I <源码>/include`。私有装配／解析头不进入映射；生成映射只是共置源码头的包含视图。

## 验证

启用 HTTP 目标时，`check-core` 包含的 `check-http-plugin` 加载真实 HTTP 动态库并发送真实回环 HTTP 请求。
它验证接口键、路由器身份、路径／查询路由、二进制 POST 正文、响应状态与头、无管理器更新时的 IO 工作线程派发、
所属线程停服时等待活动处理器与响应发送、新请求返回 503 且不执行路由、重复停服及端点复用。未使用替身服务端或传输。

`check-http-admission` 还通过生产工厂和真实回环请求，验证管理器超时及 readiness 异常后的资源保留。
已接纳路由继续完成响应，监听器与路由所有者仍然存活；该响应完成前后的新业务请求均返回 503。
重复停机保持管理器的终态；测试在请求排空后，由测试所有者显式清理服务端，不将其视为自动恢复或强制清理。

`check-network` 通过私有工厂保留 HTTP 重复绑定、重启、公开响应助手与无效 TOML 断言。
独立的 `tests/consumer/http` 仅链接 `ark::http`，在关闭应用／Protobuf 时分别编译公开头并拒绝私有头路径。
公共接口头消费命令见[目录结构](directory-structure_CN.md)，路由 API 见 [arknet HTTP](https://openarkstudio.github.io/arknet/zh/features/)。

## 旧 HTTP 示例的类型化创建

`examples/http_server` 与 `examples/http_client` 默认仍不参与示例构建。相邻的示例接口头支持类型化查找；
具体类仅在 cpp 中声明，继承对应示例接口的 `module_base`。私有工厂向 `plugin_base` 返回拥有型接口，
插件显式导出 `DllEntryPlugin`／`DllExitPlugin`，两个示例均以 `module_update::disabled` 注册，
保留继承的默认更新行为。服务端示例使用 `http_server_sample_module`，消除旧类名与公开服务
`http_server_module` 的冲突。动态库名仍为 `http_server_plugin` 和 `http_client_sample_plugin`，
已安装的公共头文件不变。
示例动态库和宿主需要一起重新编译。原来直接创建示例实现类的代码应改为加载插件后查找示例接口；
服务端示例改用 `http_server_sample_module` 查找。

服务端仍以四个 IO 线程请求监听 `0.0.0.0:8080`；`post_init()` 调用 `start_server()` 后仍返回成功，
包括监听启动被拒绝的情况。客户端仍向 `172.20.3.74:2379/v3/kv/put` 发送 future POST 和回调 POST，
保留原 JSON 正文、HTTP 超时默认值（IO 20 秒、连接 5 秒），再通过 etcd 客户端申请 100 秒租约；
etcd 客户端的超时默认值仍为 IO 5 秒、连接 2 秒。依赖或客户端返回空指针时，`init()` 会在解引用前
返回 `false`；Debug 构建保留管理器对缺失模块查找的断言。这些固定端点仍是需要外部环境的演示配置。
IO、传输和请求完成继续由 arknet 负责；
回调遵循公开 HTTP 客户端由管理器 update 派发的约定。

核心测试配置选择 Boost.Asio 且存在 `ark::http`、`ark::net`、`ark::etcd_client` API target 时，
`ARK_BUILD_HTTP_EXAMPLE_TESTS` 默认 `ON`，否则默认 `OFF`；显式设置 `OFF` 仍然有效。
既有 Applications CI 满足这些条件，通过 `check-core` 运行该测试；默认应用示例集合不变。
如需独立验证示例创建，配置
`-DASIO_STANDALONE=OFF -DARK_BUILD_CORE_TESTS=ON -DARK_BUILD_HTTP_EXAMPLE_TESTS=ON`，
再运行 `cmake --build core-build --target check-http-examples --parallel 2`。
该测试目标编译两个真实示例模块和动态库，调用工厂并加载导出入口，检查接口身份、配置选择、
POST／租约参数与超时、所属线程 update 回调派发、监听参数、租约所有者析构、重复停机与重新加载。
公开服务替身使测试可重复，无需监听端口或连接 etcd。这些检查验证示例创建与调用约定；
真实 HTTP 传输仍由 `check-http-plugin`／`check-network` 覆盖，本测试不证明外部 etcd 验收通过。
