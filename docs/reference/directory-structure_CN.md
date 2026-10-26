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

OSS SDK 使用类型化构造，保留全部三个公开头和类型；公开类布局发生变化，插件与消费者必须一起重建。可选构建条件及构造／导出检查见 [OSS SDK 构造迁移](directory-structure_CN.md#oss-sdk-类型化构造)。实现仍为 TODO 骨架。

头文件映射检查不代表 Consul、Kafka、OSS SDK 的运行时兼容性已经验证。Physic 与 `physics_scene` 仍依赖未受支持的旧版 PhysX 3 集成，保留明确的配置失败；此处没有宣称 PhysX 构建通过。

## 安装目录

[安装](../get-started/installation_CN.md#安装并消费-cmake-包)后，逻辑 `<ark/...>` 头位于 `<prefix>/include/ark/`。CMake 包文件位于 GNUInstallDirs 库目录下的 `cmake/ark/`，选定启动程序与运行时插件位于 `bin/`，配置样例与许可证位于 `share/ark/`。安装消费者使用 `find_package(ark CONFIG REQUIRED)` 和 `ark::core`／`ark::<plugin>`；源码消费者使用 `add_subdirectory` 和相同 target，无需指定生成映射路径。安装导出不包含源码或构建目录；arknet 等依赖仍由外部提供。`tests/install/` 保存独立安装消费者与删除临时生产者后的迁移检查。


## 内置服务端源码布局

八组服务端源码将接口、实现与插件的 hpp/cpp 共置于 `src/server/<role>/`。实现类继承 `module_base<interface_type>`，私有工厂向 `plugin_base` 提供由接口类型持有的 `std::unique_ptr`。原有九个模块保留接口 RTTI 注册名，均不启用帧更新。每个动态库显式定义 `DllEntryPlugin` 与 `DllExitPlugin`。

`login/interface/login_net_module.hpp`、`login/include/login_plugin.hpp` 等源码包含路径通过构建树中的 `src/server/compat-include/` 转发头保留，内部源码使用相邻头文件。转发清单保留原有全部 28 个头文件，包括实现与插件声明，但不包含新增的私有工厂头。该兼容映射独立于公开 `<ark/...>` 映射，不新增可安装的服务端 API。具体类的继承链变化后，C++ 消费方需要重新编译。

设置 `ARK_BUILD_APPLICATIONS=ON` 后，CMake 编译 Game、Login、Proxy、World、Master、Dir、Router 七个服务动态库。Simulate 源码采用相同创建方式，但仍不参与默认构建，并保留未受支持的 PhysX 3／MessagePack 集成配置失败。源码共置不代表该集成已经通过验证。定向创建检查及其边界见[服务器架构](../concepts/server-architecture_CN.md#验证服务端创建方式)。

## Consul 类型化构造

**ABI／布局不兼容变更：** `consul_module_impl` 现在继承 `module_base<consul_module>`，`consul_plugin` 继承 `plugin_base<consul_plugin>`。Consul 与消费者必须一起重建。原有三个公开头、`<ark/consul/...>` 路径、公开类型与请求方法继续通过 `ark::consul` 提供，接口本身不变。C++ 插件边界不承诺跨编译器的 ABI 兼容性。

私有 `consul_factory.hpp` 返回 `std::unique_ptr<consul_module>`，不映射、不安装。显式 `DllEntryPlugin` 与 `DllExitPlugin` 保留原有平台导出和日志初始化。安装时按 `consul_module` 接口 RTTI 键注册唯一模块，显式使用 `module_update::disabled`；卸载时移除并销毁该模块。实现仍继承默认 update 方法。配置存储、HTTP 客户端查找、TOML 加载、请求序列化及端点保持不变。

开启 `ARK_BUILD_CORE_TESTS=ON`、`ARK_BUILD_PROTO=ON` 与 `ARK_BUILD_CONFIG=ON` 后，`check-consul-construction` 运行真实工厂／装配的 doctest 用例，以及使用受控工厂与真实入口／出口源码的 DSO 测试。检查覆盖公开基类、工厂元数据、接口注册、重复注册拒绝、禁用 update、配置传递、所有权与重复 stop。依赖 Protobuf、toml++ 及通常的 core／arknet 依赖，无需运行时插件或 Consul 服务。对于已按上述选项配置的构建：

```sh
cmake --build build --target check-consul-construction --parallel 2
```

`ARK_BUILD_PLUGIN_CONSUL=ON` 还要求 `ARK_BUILD_APPLICATIONS=ON`，默认保持关闭。插件直接链接生成协议目标 `ark::proto`。开启核心测试后，`check-consul-plugin` 还在独立进程中检查真实 Consul DSO 的导出符号；`check-core` 包含当前条件下可用的 Consul 检查。受控工厂不执行生产 HTTP 客户端或访问 Consul 服务。这些构造检查不代表真实 Consul 互通、协议验收或任意热更新安全。

## Etcd Client 类型化构造

**ABI／布局不兼容变更：** `etcd_client_module_impl` 现在继承 `module_base<etcd_client_module>`，`etcd_client_plugin` 继承 `plugin_base<etcd_client_plugin>`。Etcd Client 与消费者必须一起重建。全部五个公开头、`<ark/etcd_client/...>` 路径和公开类型继续通过 `ark::etcd_client` 提供，`etcd_client_module` 与 `etcd_client` 接口不变。C++ 插件边界不承诺跨编译器的 ABI 兼容性。

私有 `etcd_client_factory.hpp` 返回 `std::unique_ptr<etcd_client_module>`，不映射、不安装。显式 `DllEntryPlugin`／`DllExitPlugin` 保留原有平台导出和日志初始化。注册沿用原接口 RTTI 键，显式使用 `module_update::disabled`；注销时销毁拥有的模块。HTTP 客户端查找、端点处理、超时参数顺序、租约请求与回调保持不变。原有 `keepalive` 空实现仍未完成，本次构造调整不增加协议或 Registry 能力。

Etcd Client 保留应用构建默认选中的策略，可通过 `ARK_BUILD_PLUGIN_ETCD_CLIENT=OFF` 关闭。仅构建该插件时需要现有 `nlohmann_json` 软件包（vcpkg 的 `etcd-client` feature）。开启 `ARK_BUILD_APPLICATIONS=ON`、`ARK_BUILD_PLUGIN_ETCD_CLIENT=ON`、`ARK_BUILD_PROTO=ON` 与 `ARK_BUILD_CORE_TESTS=ON` 后，运行：

```sh
cmake --build build --target check-etcd-client-plugin --parallel 1
```

该目标包含 `check-etcd-client-construction`：真实工厂／装配的 doctest 用例，以及使用生产入口／出口源码与受控工厂的 DSO 测试，覆盖配置传递、接口元数据、禁用 update、所有权恰好销毁一次和重复 stop。另一个独立进程加载真实生产插件，检查入口／出口导出后卸载。选中运行时插件时，`check-core` 包含这两项检查；关闭插件的核心构建不增加 JSON 依赖。应用 CI 显式启用该插件，通过现有 `check-core` 任务执行检查。

这些测试不启动 Etcd 服务，不执行生产 HTTP／Etcd 互通验收，只验证构造与 DSO 边界，不代表 Registry 已完成，也不证明任意热更新安全。

## OSS SDK 类型化构造

**ABI／布局不兼容变更：** `oss_sdk_module_impl` 现在继承 `module_base<oss_sdk_module>`，`oss_sdk_plugin` 继承 `plugin_base<oss_sdk_plugin>`。OSS SDK 与消费者必须一起重建。全部三个公开头、`<ark/osssdk/...>` 路径和公开类型继续通过 `ark::osssdk` 提供，`oss_sdk_module` 接口不变。C++ 插件边界不承诺跨编译器的 ABI 兼容性。

私有 `oss_sdk_factory.hpp` 返回 `std::unique_ptr<oss_sdk_module>`，不映射、不安装。显式 `DllEntryPlugin`／`DllExitPlugin` 保留平台导出和日志初始化。安装时沿用 `oss_sdk_module` 接口 RTTI 键，继续禁用 update。类型化注销替代未定义的 `ARK_DEREGISTER_MODULE` 调用，销毁拥有的模块。配置存储和继承的默认 update 方法保持不变。实现仍为 TODO 骨架，不增加 OSS 客户端、请求 API 或对象存储协议。

开启 `ARK_BUILD_CORE_TESTS=ON` 后，`check-oss-sdk-construction` 将真实工厂与装配编入 doctest 用例，并使用生产入口／出口源码与带计数的受控工厂构建 DSO。检查覆盖保留的公开基类／类型、工厂元数据、接口注册、重复注册拒绝、禁用 update、配置传递、恰好销毁一次和重复 stop。该目标仅需要通常的 core／arknet 依赖，无需运行时插件、Protobuf、JSON 软件包或对象存储服务：

```sh
cmake --build build --target check-oss-sdk-construction --parallel 1
```

运行时插件默认保持关闭。开启 `ARK_BUILD_APPLICATIONS=ON`、`ARK_BUILD_PLUGIN_OSSSDK=ON`、`ARK_BUILD_PROTO=ON` 与 `ARK_BUILD_CORE_TESTS=ON` 后，还可构建真实 `ark_oss_sdk_plugin` DSO，并在独立进程中检查入口／出口导出：

```sh
cmake --build build --target check-oss-sdk-plugin --parallel 1
```

应用构建仍需要现有 Protobuf、配置模块与 Boost.Asio。`check-core` 包含当前条件下可用的 OSS SDK 构造／导出检查，不增加外部 OSS SDK 依赖。这些测试只验证构造与 DSO 边界，不代表 OSS 客户端功能、服务互通或任意热更新安全已经通过验收。

## Kafka 类型化构造

**ABI／布局不兼容变更：**`kafka_module_impl` 改为继承 `module_base<kafka_module>`，`kafka_plugin` 改为继承 `plugin_base<kafka_plugin>`。Kafka 与消费者必须一起重建。原有四个公开头、`<ark/kafka/...>` 路径和公开类型继续通过 `ark::kafka` 提供。模块接口、生产者／消费者封装及 `RdKafka::DeliveryReportCb`、`RdKafka::EventCb` 两个基类均保留。C++ 插件边界不承诺跨编译器 ABI 兼容。

私有 `kafka_factory.hpp` 返回 `std::unique_ptr<kafka_module>`，不进入公开映射或安装。显式 `DllEntryPlugin`／`DllExitPlugin` 保留平台导出与日志初始化。装配保留接口 RTTI 注册键，并显式选择 `module_update::enabled`，继续定期轮询生产者。注销会销毁所属模块。生产者存储与所有权、生产者创建、投递／事件回调及消息行为均不变。

Kafka 继续作为可选插件，**默认关闭**。原有依赖发现与链接方式不变：启用 `ARK_BUILD_APPLICATIONS=ON`、`ARK_BUILD_PROTO=ON`、`ARK_BUILD_CORE_TESTS=ON`、`ARK_BUILD_PLUGIN_KAFKA=ON`，并准备对应平台的 Kafka 驱动依赖。Unix 平台包括 librdkafka C／C++、OpenSSL、Zstandard、LZ4 和 zlib。完成配置后运行：

```sh
cmake --build build --target check-kafka-plugin --parallel 1
```

`check-kafka-plugin` 包含 `check-kafka-construction`：真实工厂／装配 doctest，以及使用生产入口／出口源码、替换私有工厂的受控动态库。检查覆盖类型化基类、接口元数据、重复注册、启用 update、配置传递、仅销毁一次的所有权与重复停机。独立进程加载真实生产动态库，检查入口／出口导出后卸载。仅选择 Kafka 运行时目标时，`check-core` 才纳入这些检查；关闭插件的核心测试不发现 Kafka 依赖。

回调日志将 RdKafka 错误码、严重等级和事件类型枚举显式转换为整数，以兼容当前 fmt，并保留原有数值信息。生产者行为、回调顺序与依赖发现方式均不变。

这些检查不创建生产者，也不连接 broker。它们验证构造与动态库边界，不代表 Kafka 消息投递、flush 行为、服务互通或任意热更新安全性。可选驱动构建仍需单独进行各平台验收。
