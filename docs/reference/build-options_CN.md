# 构建选项

| 选项 | 默认值 | 用途 |
| --- | --- | --- |
| `ARK_BUILD_APPLICATIONS` | `ON` | 构建应用及选中的运行时插件 |
| `ARK_BUILD_CONFIG` | 应用或核心测试启用时为 `ON`，否则为 `OFF` | 构建应用／插件共享配置读取器；网络回归也会为 HTTP 构建读取器 |
| `ARK_BUILD_PROTO` | 与 `ARK_BUILD_APPLICATIONS` 一致 | 生成并构建协议消息；构建应用时必须启用 |
| `ARK_BUILD_NETWORK_TESTS` | `OFF` | 构建独立 C++ 网络与日志回归测试 |
| `ARK_BUILD_TESTS` | `ON` | 构建原有测试程序 |
| `ARK_BUILD_CORE_TESTS` | 与 `ARK_BUILD_TESTS` 一致 | 构建 C++20 核心与可选的 Protobuf 回归测试 |
| `ARK_BUILD_EXAMPLES` | `ON` | 构建选中的示例 |
| `ARK_BUILD_HTTP_EXAMPLE_TESTS` | 核心测试使用 Boost 且具备 HTTP／Net／Etcd Client API target 时为 `ON`，否则为 `OFF` | 通过独立服务替身编译并运行真实 HTTP 示例动态库；保留显式 `OFF` |
| `ARK_BUILD_PLUGIN_MYSQL` | `OFF` | 构建原生 MySQL 协程插件，要求 `ARKNET_ENABLE_SSL=ON` |
| `ARK_BUILD_PLUGIN_REDIS` | `OFF` | 构建原生 Redis 协程插件，TLS 连接池需要开启 SSL |
| `ARK_BUILD_DATABASE_TESTS` | `OFF` | 构建原生数据库及公开插件回归，要求显式设置 `ARKNET_SOURCE_DIR` |
| `ARK_BUILD_BENCHMARKS` | `OFF` | 构建独立 C++ 基准程序 |
| `ARK_USE_GUID_32` | `OFF` | 使用 32 位 GUID 配置；公共接口目标向调用方传递该设置 |
| `ARKNET_SOURCE_DIR` | 空 | 使用独立的本地 arknet 源码 |
| `ARKNET_REVISION` | 仓库固定的 commit | 指定获取 arknet 时使用的版本；已有 CMake 缓存会保留原值 |
| `ASIO_STANDALONE` | `OFF` | `OFF` 选择 Boost，`ON` 选择 Standalone Asio |

项目通过 CMake 要求 C++20，并禁用编译器扩展。应用和网络测试需要 Boost.Asio 与 Boost.Beast。仅构建核心或原生数据库开发测试时可以选择 Standalone Asio，须明确关闭应用和网络测试；不支持的组合会在获取依赖前由 CMake 报错。本地 arknet 路径是开发覆盖项，不会把网络库复制进 ark。

升级 arknet 时，使用新的构建目录，或显式修改缓存中的引用。`ARKNET_SOURCE_DIR` 非空时优先使用本地源码，不按固定 commit 获取依赖；具体操作见[升级 arknet](../get-started/installation_CN.md#升级-arknet)。

官方插件选项采用 `ARK_BUILD_PLUGIN_<NAME>`。默认运行时插件为 `kernel`、`net`、`bus`、`utility` 和 `etcd_client`；启用 MySQL 或 Redis 构建选项可使用原生协程插件；本机协议测试与插件加载检查不能替代真实数据库部署验收。参见[项目结构](directory-structure_CN.md)、[协议生成](protobuf_CN.md)与[网络构建](../networking_CN.md)。

开启 `ARK_BUILD_EXAMPLES` 时，[协程任务示例](../guides/tasks_CN.md)可以在关闭应用的情况下独立构建；默认启用的应用插件示例仍要求开启应用构建。HTTP 示例创建测试可以随核心测试单独运行，详见 [HTTP 示例](http_CN.md#旧-http-示例的类型化创建)。

数据库插件的配置与接口见 [MySQL](mysql_CN.md) 和 [Redis](redis_CN.md)。

## 原生数据库插件

按安装指南准备依赖后，可同时构建两个可选插件：

```sh
cmake -S . -B build -DARK_BUILD_APPLICATIONS=ON \
  -DARK_BUILD_PLUGIN_MYSQL=ON -DARK_BUILD_PLUGIN_REDIS=ON -DARKNET_ENABLE_SSL=ON
cmake --build build --target ark_mysql_plugin ark_redis_plugin
```

全部官方插件的公开头通过显式清单在配置阶段复制到各构建树生成的插件公开头映射。各 `ark::<name>` API target 传递该目录，插件关闭时仍可使用。MySQL、Redis、Utility、Net、Bus、HTTP 的私有装配头和工厂不进入映射；Kernel 保留包括 `kernel_plugin.hpp` 在内的原有 38 个头；Consul、Etcd Client、Kafka、OSS SDK、Physic 保留包括实现头与装配头在内的原有 22 个公开头。注册键、公开类型可见性和可选插件策略继续保留；[Kernel 类型化构造](kernel_CN.md#kernel-类型化构造)与 [Consul 类型化构造](directory-structure_CN.md#consul-类型化构造)改变公开类布局，需要重建消费者。两者的私有工厂头均不映射、不安装；头文件映射不代表可选驱动集成已经验证。每个公共头仅有一份共置源码；生成的头文件映射属于选定构建树，不构成第二棵源码目录。

应用 CI 在 Linux、macOS、Windows 启用可选 Consul 插件，`check-core` 包含构造、动态库生命周期与生产库导出检查。这些检查不需要 Consul 服务，也不代表服务互通验收。

业务插件链接 `ark::mysql` 和/或 `ark::redis`，以获取公开头文件和编译要求；公共接口目标不会加载运行时动态库。将所需动态库与启动器一起部署，并在应用清单中填写库名称：

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "ark_mysql_plugin"
config = ""

[[plugins]]
name = "ark_redis_plugin"
config = ""
```

只保留应用所需的插件。这里的 `config` 为空：业务模块通过公开接口配置具名连接，不再使用旧版数据库 XML 文件。平台扩展名、Debug 后缀和公共接口兼容要求见[运行时选择动态库](../guides/plugins_CN.md#在运行时选择动态库)。在业务模块初始化时查找数据库模块，再通过[托管任务组](../guides/tasks_CN.md)启动操作。

核心头文件位于 `src/core/`；`ark::core` 传递公开头的包含目录，保留 `<ark/...>` 引用。启动器源码位于 `src/application/`，输出仍为 `app` 和 `app_d`。

## 安装与归档

`ARK_INSTALL` 在顶层项目中默认 `ON`，嵌入构建时默认 `OFF`；启用显式公开头安装、可迁移 CMake 导出、`check-install` 与 TGZ／ZIP CPack 归档。`ARK_USE_INSTALLED_ARKNET=ON` 发现单独安装的 arknet 包，优先于源码与版本覆盖；默认 `OFF`，保留源码依赖构建。具体操作见[安装与包消费](../get-started/installation_CN.md#安装并消费-cmake-包)。打包不会启用可选运行时插件，也不代表外部驱动已经验收。

Net 的 18 个公开头不再包含 `http_client_module_impl.hpp`；使用 `<ark/net/http_client_module.hpp>` 并查找公开接口。此移除属于源码／ABI 兼容性变化，详见 [Net 迁移](network_CN.md#api-与私有创建)。私有工厂声明不安装。
