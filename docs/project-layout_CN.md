# 项目结构与公共插件接口

[English](project-layout.md) | 简体中文

ark 使用 C++20。核心与官方插件放在本仓库，业务插件放在应用仓库。
网络库继续维护在独立的 [arknet 仓库](https://github.com/OpenArkStudio/arknet)。

## 目录

构建产物保存在各自的 CMake 构建目录中：`cmake -S . -B out/release` 将可执行文件、
插件和静态库输出到 `out/release/bin`。Debug 和 sanitizer 构建使用不同目录，避免产物
相互覆盖。使用 `-B build` 时仍输出到 `build/bin`。部署时，将所选构建的产物复制到
运行目录的 `bin/` 下。

| 路径 | 内容 |
| --- | --- |
| `src/core/` | 核心头文件、实现与 CMake 使用要求 |
| `src/application/` | 应用入口与停服请求 |
| `src/proto/` | 协议定义与编解码实现 |
| `src/server/` | 内置服务端应用 |
| `src/plugins/<name>/` | 全部官方插件头文件、实现与装配 |
| `tests/` | 核心、网络与私有数据库回归测试 |
| `tests/network/` | 网络与日志回归测试 |
| `tests/consumer/` | 独立使用公共插件接口的示例项目 |
| `examples/` | 应用示例 |
| `config/` | 应用清单、插件和日志配置样例 |
| `resources/` | 业务数据、工作表与历史资源 |
| `tools/` | 离线配置工具与保留的历史工具 |
| `deployment/scripts/` | 单应用启动与显式 PID 停止脚本 |
| `deployment/` | 容器构建文件与 Kubernetes 示例 |

使用 `<ark/plugin_manager.hpp>`、`<ark/net/tcp_client.hpp>` 等公开路径包含头文件，
不需要把实现目录加入应用的包含路径。Protobuf 生成文件位于构建目录，由 `ark::proto` 提供。

核心头文件只有 `src/core/` 中的一份源码。`ark::core` 将显式公开清单映射到选定构建目录的 `src/core/public-include/ark/`，保留 `<ark/plugin_manager.hpp>` 等包含路径。修改源码头文件，不编辑生成副本。启动程序仍为 `app`／`app_d`，清单格式保持不变。

## CMake 目标

| 目标 | 用途 |
| --- | --- |
| `ark::core` | 核心头文件、C++20 要求与公共编译定义 |
| `ark::net`、`ark::kernel`、`ark::utility` 等 | 插件头文件及其公开 API 依赖 |
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

详细命名规则、本地检查方式和协议迁移说明见[代码风格](code-style_CN.md)。

## 验证公共接口头使用方式

按[网络构建与回归测试](networking_CN.md)安装依赖后，
编译独立的公共接口头使用示例：

```sh
cmake -S tests/consumer -B sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF
cmake --build sdk-build --target check-sdk
```

该示例关闭应用和 Protobuf 构建，检查公开头文件路径、C++20 编译要求、模块继承和导出的 GUID 配置。
增加 `-DARK_USE_GUID_32=ON` 可检查 32 位 GUID 配置。

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

Kernel 的原有 38 个头与 cpp 共置于 `src/plugins/kernel/`，通过 `ark::kernel` 保留全部 `<ark/kernel/...>` 路径，包括 `data_impl.hpp`、`data_list_impl.hpp` 和 `kernel_plugin.hpp`。类型化构造保留公开类型与注册键；按[构造迁移](reference/kernel_CN.md#kernel-类型化构造)说明一起重建 Kernel 与消费者。消费检查及 Protobuf 条件见 [Kernel 参考](reference/kernel_CN.md)。

Consul、Etcd Client、Kafka、OSS SDK、Physic 的原有 22 个头也与实现共置于 `src/plugins/<name>/`。显式映射保留原有全部 `<ark/consul/...>`、`<ark/etcd_client/...>`、`<ark/kafka/...>`、`<ark/osssdk/...>`、`<ark/physic/...>` 路径，包括实现头和装配头。通过对应的 `ark::<name>` API target 消费；关闭运行时插件后仍可使用映射。CMake 不再从 `include/ark/` 发现插件头文件。

`node --test tests/ci/plugin-layout.test.mjs` 在全部运行时插件选项关闭时，检查配置阶段的头文件映射、副本字节一致性、API 依赖与失效映射清理。该检查不编译可选驱动集成。Etcd Client 消费者在关闭 Protobuf 和运行时插件的情况下编译原有五个头：

```sh
cmake -S tests/consumer/etcd_client -B etcd-client-headers -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF
cmake --build etcd-client-headers --target check-etcd-client-headers --parallel 2
```

头文件映射检查不代表 Consul、Kafka、OSS SDK 的运行时兼容性已经验证。Physic 与 `physics_scene` 仍依赖未受支持的旧版 PhysX 3 集成，保留明确的配置失败；此处没有宣称 PhysX 构建通过。
