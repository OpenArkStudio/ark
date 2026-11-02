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

| 必填字段 | TOML 约束 |
| --- | --- |
| `host` | 非空、无 NUL 的字符串 |
| `port` | `0..65535` 的整数 |
| `threads` | `1..255` 的整数 |

未知键、缺失／错误类型及越界使 `init()` 失败，诊断带文件／字段；语法错误还带行列。旧 XML 改名不会转换内容，见[插件配置](../service-manifests_CN.md#http-与-consul-插件配置)。初始化仅解析，不启动监听。

<Aside type="note" title="区分配置值与重载默认值">

`start_server()` 使用配置值；显式重载的 `port = 8080`、`thread_num = 1` 不是 TOML 默认。零端口由 OS 分配；模块不公开所选端口，客户端需要发现地址时应配置已知端口。

</Aside>

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

| 监听／停止边界 | 行为 |
| --- | --- |
| 端点预留 | 运行中拒绝再次启动；同库两实例不能占用同一非零端点；单独调用 `pre_shut()` 停止并释放服务／预留后，仍可重启端点 |
| 普通请求 | 不按管理器状态关业务入口或返回 HTTP 503；路由保持普通行为直到资源就绪封闭；之后的传输请求关闭且不调用路由 |
| `ready_to_shut()` | 原子封闭原生路由派发，等待活跃路由与响应发送持有状态结束；失败发送也释放持有，不证明客户端收到响应 |
| 资源关闭 | `pre_shut()` 停止并等待原生服务退出，释放端点；阻塞路由须协作返回，独立后台工作需另外持有 |

不提供通用插件热替换或 HTTP/2；arknet 仍拥有 IO 工作线程、请求解析和传输。

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

`check-http-plugin` 使用真实 HTTP DSO 和回环请求检查公开接口、路由、IO 线程派发、资源就绪、重复关闭与端点复用。`check-http-admission` 保留历史目标名，用于检查资源等待／保留；当前不提供按管理器状态的业务拒绝门。验收须对应实际修订和执行结果，旧 HTTP 503 场景不是当前合同证据。独立调度的路由工作不计入 HTTP 发送持有状态。

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
