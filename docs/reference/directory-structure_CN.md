# 项目结构与公共插件接口

ark 使用 C++20。核心与官方插件放在本仓库，业务插件放在应用仓库。
网络库继续维护在独立的 [arknet 仓库](https://github.com/OpenArkStudio/arknet)。

## 目录

构建产物归属于各自的 CMake 构建目录：`cmake -S . -B out/release` 将可执行文件、插件与归档输出到 `out/release/bin`。Debug 和 sanitizer 构建使用不同目录，避免产物互相覆盖。`-B build` 仍输出到 `build/bin`；部署时将所选构建的产物复制到运行目录的 `bin/`。

| 路径 | 内容 |
| --- | --- |
| `src/core/` | 核心头文件、实现与 CMake 使用要求 |
| `src/application/` | 应用入口与停服请求 |
| `src/config/` | 应用清单声明、共享的文件／TOML 读取实现与私有诊断 |
| `src/proto/` | 协议定义与 Protobuf 消息生成的 CMake 规则 |
| `src/server/` | 内置服务端应用 |
| `src/plugins/<name>/` | 全部官方插件头文件、实现与装配 |
| `tests/` | 核心、网络与私有数据库回归测试 |
| `tests/network/` | 网络与日志回归测试 |
| `tests/consumer/` | 独立使用公共插件接口的示例项目 |
| `examples/` | [能力示例](../guides/examples_CN.md)，hpp/cpp 继续并排存放 |
| `config/` | 应用清单、插件和日志配置样例 |
| `resources/` | 业务数据、工作表与历史资源 |
| `tools/` | 离线配置工具与保留的历史工具 |
| `deployment/scripts/` | 单应用启动与显式 PID 停止脚本 |
| `deployment/` | 容器构建文件与 Kubernetes 示例 |

使用 `<ark/plugin_manager.hpp>`、`<ark/net/tcp_client.hpp>` 等公开路径包含头文件，
不需要把实现目录加入应用的包含路径。Protobuf 生成文件位于构建目录，由 `ark::proto` 提供。

MySQL、Redis 的唯一公开接口源码分别是 `src/plugins/mysql/mysql_module.hpp`、`src/plugins/redis/redis_module.hpp`。Utility 的公开接口与保留的调度辅助头位于 `src/plugins/utility/`；Net 的公开接口、端点模板与传输辅助类型位于 `src/plugins/net/`。Net 将私有插件装配与具体 HTTP 客户端／服务管理器定义排除在公开映射之外。Bus 的两个公开模块头位于 `src/plugins/bus/`；共享核心值类型 `<ark/bus.hpp>` 的源码位于 `src/core/bus.hpp`。HTTP 映射公开的 `src/plugins/http/http_server_module.hpp`，排除私有插件与具体类。CMake 在配置阶段按显式清单复制普通文件到各构建树生成的插件公开头映射；清理该受控目录中已移除的映射，MySQL、Redis、Utility、Net、Bus、HTTP 不复制私有装配头或工厂；Kernel 保留包括 `kernel_plugin.hpp` 在内的原有 38 个头，具体类型与装配可见性不变。通过 `ark::mysql`、`ark::redis`、`ark::utility`、`ark::net`、`ark::bus`、`ark::http`、`ark::kernel` 消费，逻辑包含路径保持不变，插件关闭时同样有效。仅手写 `-I <源码>/include` 的调用需要改用目标传递的包含目录。映射副本是生成的构建产物；应编辑功能目录中的共置源码头。

核心头文件只有 `src/core/` 中的一份源码。`ark::core` 将显式公开清单映射到选定构建目录，保留 `<ark/plugin_manager.hpp>` 等包含路径。修改源码头文件，不编辑生成副本。启动程序仍为 `app`／`app_d`，清单格式保持不变。

## CMake 目标

| 目标 | 用途 |
| --- | --- |
| `ark::core` | 核心头文件、C++20 要求与公共编译定义 |
| `ark::net`、`ark::kernel`、`ark::utility` 等 | 插件头文件及其公开 API 依赖 |
| `ark::config_headers` | 应用清单声明，关闭编译读取器时仍可用 |
| `ark::config` | 共享的配置读取实现，不链接网络库 |
| `ark::proto` | 生成的协议消息及其编译库，需要 `ARK_BUILD_PROTO=ON` |
| `ark::ark` | 多插件应用使用的公共接口聚合目标 |

核心与插件 API 目标负责传递 CMake 编译要求，不负责加载运行时插件。
设置 `ARK_BUILD_APPLICATIONS=ON` 后，通过 `ARK_BUILD_PLUGIN_<NAME>` 选择要编译的插件动态库。
现有动态库文件名和配置中的插件名称保留；使用新 C++ API 的应用需要重新编译。

```cmake
add_subdirectory(path/to/ark ark-build)
add_executable(my_service main.cpp)
target_link_libraries(my_service PRIVATE ark::core ark::net)
```

默认编译 `kernel`、`net`、`bus`、`utility`、`etcd_client` 插件。
其他现有插件按需启用，并需要对应驱动依赖；目录迁移不代表这些集成已经通过验证。

## 命名与格式

- 命名空间、类型、函数和文件名使用 `snake_case`，例如 `ark::plugin_manager`、
  `find_module()`、`plugin_manager.hpp`。
- 私有和受保护成员以下划线结尾，例如 `module_name_`。
- include 顺序为系统头文件、第三方头文件、ark 头文件。
- 使用 clang-format 18 和仓库内的 `.clang-format`。第三方与生成文件不参与项目格式化。

## 验证公共接口头使用方式

按[网络构建与回归测试](../networking_CN.md)安装依赖后，
编译独立的公共接口头使用示例：

```sh
cmake -S tests/consumer -B sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF -DARKNET_ENABLE_SSL=ON \
  -DVCPKG_MANIFEST_FEATURES=database
cmake --build sdk-build --target check-sdk
```

该示例关闭应用和 Protobuf 构建，检查公开头文件路径、C++20 编译要求、模块继承和导出的 GUID 配置。
增加 `-DARK_USE_GUID_32=ON` 可检查 32 位 GUID 配置。

消费者还验证 MySQL/Redis/Utility 的公开头映射，并拒绝可见的实现或私有装配头。上述命令启用 SSL 和 vcpkg `database` feature 所需的 OpenSSL，同时检查两种接口；关闭 SSL 时仅编译 Redis 接口。这里验证选定构建树中的公共接口头消费。

仅使用 Net 的消费者启用 Protobuf，保持运行时插件关闭；分别编译 18 个 Net 公开头，
并确认私有管理器与插件头不可见：

```sh
cmake -S tests/consumer/net -B net-sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES=application -DASIO_STANDALONE=OFF
cmake --build net-sdk-build --target check-sdk --parallel 2
```

仅使用 Bus 的消费者同样启用 Protobuf，分别编译两个映射的公开模块头，
确认私有实现、装配与解析头不可见。共享核心值类型 `<ark/bus.hpp>` 保持原有源码路径。

```sh
cmake -S tests/consumer/bus -B bus-sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES=application -DASIO_STANDALONE=OFF
cmake --build bus-sdk-build --target check-sdk --parallel 2
```

仅使用 HTTP 的消费者不需要 Protobuf 或运行时插件；它编译映射的公开模块与路由 API，并排除私有装配／解析头。

```sh
cmake -S tests/consumer/http -B http-sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF
cmake --build http-sdk-build --target check-sdk --parallel 2
```

## 运行资源与生成数据

源码资产与运行包分别组织。`build/` 只作为可选的 CMake 构建目录，不再保存受版本控制的运行文件。

| 源码路径 | 用途与部署位置 |
| --- | --- |
| `config/` | 配置样例；将所需文件复制到运行目录的 `conf/` |
| `resources/excel/` | 离线编辑的配置工作表 |
| `resources/meta/`、`resources/server/` | 生成的元数据与业务数据；部署到运行目录的 `resource/` |
| `resources/client/`、`resources/mysql/` | 客户端输出目录占位和数据库结构资源 |
| `resources/code/` | 复制到公开头文件前的元数据生成代码 |
| `tools/` | 配置导出器和保留的历史工具；状态见工具指南 |
| `deployment/scripts/` | 启动和停止脚本；可复制到运行目录，或显式传入运行目录 |

运行包继续使用 `bin/`、`conf/` 和按需提供的 `resource/`；日志由配置写入运行目录的 `binlog/`。源码目录改名不改变业务 XML、资源格式或运行时查找规则。见[运行脚本迁移](../guides/applications_CN.md#迁移旧命令)。

旧文中的 `bin/`、`frame/`、`dep/` 目录不是当前源码布局。生成流程见[配置工具](../guides/tools_CN.md)，进程启动见[应用启动器](../guides/applications_CN.md)。协议公开头文件与兼容边界见 [Protobuf 消息](protobuf_CN.md)。

Kernel 的原有 38 个头与 cpp 共置于 `src/plugins/kernel/`，通过 `ark::kernel` 保留全部 `<ark/kernel/...>` 路径，包括 `data_impl.hpp`、`data_list_impl.hpp` 和 `kernel_plugin.hpp`。保留的公开实现类／插件类使用类型化构造；按 [Kernel 构造迁移](kernel_CN.md#kernel-类型化构造)说明一起重建受影响的消费者。注册键、update 选择与生命周期顺序保持不变。消费检查及 Protobuf 条件见 [Kernel 参考](kernel_CN.md)。

Consul、Etcd Client、Kafka、OSS SDK、Physic 的原有 22 个头也与实现共置于 `src/plugins/<name>/`。显式映射保留原有全部 `<ark/consul/...>`、`<ark/etcd_client/...>`、`<ark/kafka/...>`、`<ark/osssdk/...>`、`<ark/physic/...>` 路径，包括实现头和装配头。通过对应的 `ark::<name>` API target 消费；关闭运行时插件后仍可使用映射。CMake 不再从 `include/ark/` 发现插件头文件。

`node --test tests/ci/plugin-layout.test.mjs` 在全部运行时插件选项关闭时，检查配置阶段的头文件映射、副本字节一致性、API 依赖与失效映射清理。该检查不编译可选驱动集成。Etcd Client 消费者在关闭 Protobuf 和运行时插件的情况下编译原有五个头：

```sh
cmake -S tests/consumer/etcd_client -B etcd-client-headers -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF
cmake --build etcd-client-headers --target check-etcd-client-headers --parallel 2
```

头文件映射检查不代表 Consul、Kafka、OSS SDK 的运行时兼容性已经验证。Physic 与 `physics_scene` 仍依赖未受支持的旧版 PhysX 3 集成，保留明确的配置失败；此处没有宣称 PhysX 构建通过。

## 安装目录

[安装](../get-started/installation_CN.md#安装并消费-cmake-包)后，逻辑 `<ark/...>` 头位于 `<prefix>/include/ark/`。CMake 包文件位于 GNUInstallDirs 库目录下的 `cmake/ark/`，选定启动程序与运行时插件位于 `bin/`，配置样例与许可证位于 `share/ark/`。安装消费者使用 `find_package(ark CONFIG REQUIRED)` 和 `ark::core`／`ark::<plugin>`；源码消费者使用 `add_subdirectory` 和相同 target，无需指定生成映射路径。安装导出不包含源码或构建目录；arknet 等依赖仍由外部提供。`tests/install/` 保存独立安装消费者与删除临时生产者后的迁移检查。


## 内置服务端源码布局

八组服务端源码将接口、实现与插件的 hpp/cpp 共置于 `src/server/<role>/`。实现类继承 `module_base<interface_type>`，私有工厂向 `plugin_base` 提供由接口类型持有的 `std::unique_ptr`。原有九个模块保留接口 RTTI 注册名，均不启用帧更新。每个动态库显式定义 `DllEntryPlugin` 与 `DllExitPlugin`。

`login/interface/login_net_module.hpp`、`login/include/login_plugin.hpp` 等源码包含路径通过构建树中的 `src/server/compat-include/` 转发头保留，内部源码使用相邻头文件。转发清单保留原有全部 28 个头文件，包括实现与插件声明，但不包含新增的私有工厂头。该兼容映射独立于公开 `<ark/...>` 映射，不新增可安装的服务端 API。具体类的继承链变化后，C++ 消费方需要重新编译。

设置 `ARK_BUILD_APPLICATIONS=ON` 后，CMake 编译 Game、Login、Proxy、World、Master、Dir、Router 七个服务动态库。Simulate 源码采用相同创建方式，但仍不参与默认构建，并保留未受支持的 PhysX 3／MessagePack 集成配置失败。源码共置不代表该集成已经通过验证。定向创建检查及其边界见[服务器架构](../concepts/server-architecture_CN.md#验证服务端创建方式)。
