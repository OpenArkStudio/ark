# 安装与构建

ark 需要 CMake 3.21+、Ninja、支持 C++20 的编译器，以及 vcpkg manifest 选定的依赖。Windows 使用 Visual Studio 2022 Developer PowerShell，macOS/Linux 使用兼容的 Clang 或 GCC 工具链。

<Steps>
<Step title="克隆仓库">

```sh
git clone https://github.com/OpenArkStudio/ark.git
cd ark
```

</Step>
<Step title="配置应用构建">

安装 vcpkg，将环境变量 `VCPKG_ROOT` 指向对应目录。网络依赖选择 Boost。

ark 的公开目标会启用 MSVC 标准预处理器模式（`/Zc:preprocessor`）。自定义目标通过 `ark::core` 继承该选项；手动编译 ark 头文件时，需要同时启用 C++20 和此选项。

<Tabs syncKey="build-platform">

<TabItem label="macOS/Linux">

使用 POSIX shell。下方的 [macOS 编译器与 SDK 示例](#macos-编译器与-sdk)显式选择 Apple 工具链。

```sh
cmake -S . -B app-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES="application;boost" \
  -DASIO_STANDALONE=OFF -DCMAKE_BUILD_TYPE=Release
```

</TabItem>

<TabItem label="Windows PowerShell">

```powershell
cmake -S . -B app-build -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_MANIFEST_FEATURES=application;boost" `
  -DASIO_STANDALONE=OFF -DCMAKE_BUILD_TYPE=Release
```

</TabItem>

</Tabs>

</Step>
<Step title="构建">

```sh
cmake --build app-build --parallel 2
```

使用多配置生成器时还需添加 `--config Release`。该命令将可执行文件、插件和静态库输出到 `app-build/bin/`。Release、Debug 和 sanitizer 构建分别使用独立的 CMake 目录。按[快速开始](quick-start_CN.md)将选定的二进制文件复制到运行目录。[构建选项](../reference/build-options_CN.md)可控制应用、示例和测试。

</Step>
</Steps>

只验证网络正确性时，按[网络回归测试](../networking_CN.md)进行精简构建。准备独立运行目录后，使用 `deployment/scripts/` 下的[启动脚本](../guides/applications_CN.md#启动一个应用)，并显式传入运行目录。

## macOS 编译器与 SDK

通过 `xcrun` 选择当前 Xcode 或 Command Line Tools 中配套的 C 编译器、C++ 编译器和 SDK。更换编译器或 SDK 时，使用新的构建目录，避免 CMake 保留原有选择。用以下命令替代上面的配置与构建命令：

```sh
ark_macos_sdk="$(xcrun --sdk macosx --show-sdk-path)"
cmake -S . -B app-build-macos -G Ninja \
  -DCMAKE_C_COMPILER="$(xcrun --sdk macosx --find clang)" \
  -DCMAKE_CXX_COMPILER="$(xcrun --sdk macosx --find clang++)" \
  -DCMAKE_OSX_SYSROOT="$ark_macos_sdk" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES="application;boost" \
  -DASIO_STANDALONE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build app-build-macos --parallel 2
```

后续复制运行文件时，将示例路径中的 `app-build` 替换为 `app-build-macos`。

<Aside type="caution" title="Homebrew LLVM 22.1.7 与 macOS SDK 27.0">

这组编译器与 SDK 存在与包含顺序有关的标准头文件兼容问题，可能报 `INFINITY` 或 `DBL_EPSILON` 未声明。遇到这些错误时，按上面的命令选择配套的 Apple Clang 和 SDK，并使用新的构建目录。

</Aside>

## 归档插件

Consul、Etcd Client、Kafka 和 OSS SDK 已[归档](../reference/directory-structure_CN.md#归档集成)，四个运行时插件均默认关闭；上述安装与默认示例不需要这四项服务。源码、公开头、CMake target 和显式启用选项继续保留，安装公共头不代表运行时功能或外部服务验收。仅在明确需要保留的集成时选择对应 `ARK_BUILD_PLUGIN_<NAME>=ON` 并准备原有依赖；Etcd Client 还需增加 vcpkg `etcd-client` feature。

已有 CMake 缓存会保留旧值，包括 Etcd Client 原有的 `ON`。使用新构建目录，或显式传入 `-DARK_BUILD_PLUGIN_ETCD_CLIENT=OFF`，以采用归档后的默认选择。现有 CI 显式启用的兼容性检查仍保留。

## 升级 arknet

由 CMake 获取网络库时，仓库中的 `ARKNET_REVISION` 默认值指定一个固定的 arknet commit。更新 ark 源码不会替换已有 `CMakeCache.txt` 中保存的 `ARKNET_REVISION`。

使用新的构建目录并重新执行上面的配置命令，即可采用仓库默认值。需要保留现有构建目录时，只清除缓存中的版本引用，让 CMake 采用更新后 ark 源码的默认值：

```sh
cmake -U ARKNET_REVISION -S . -B app-build
cmake --build app-build --parallel 2
```

已有构建目录会保留其他配置选项。核对 `app-build/CMakeCache.txt` 中的 `ARKNET_REVISION`；使用多配置生成器时，构建仍需添加 `--config Release`。需要显式选择其他版本时，将 `ARKNET_REVISION` 设为对应的完整 40 位 commit SHA。[依赖管理](../dependencies_CN.md)列出仓库固定提交及其是否属于已发布版本。

`ARKNET_SOURCE_DIR` 非空时，CMake 直接使用该本地源码，不会按 `ARKNET_REVISION` 获取版本。开发时需显式更新这个源码目录。需要恢复按仓库固定 commit 获取依赖时，配置 `-DARKNET_SOURCE_DIR=` 和 `-U ARKNET_REVISION`。这些 CMake 覆盖项仍可使用；CI 策略只核对仓库默认引用，不检查开发者的缓存或本地源码。

## 安装并消费 CMake 包

顶层构建默认启用 `ARK_INSTALL`；通过 `add_subdirectory` 嵌入时默认关闭。先构建选定产物，再执行安装：

```sh
cmake --build app-build --parallel 2
cmake --install app-build --prefix "$PWD/ark-prefix"
```

多配置生成器的两条命令都需要添加 `--config Release`。安装遵循 GNUInstallDirs：公开头位于 `include/ark/`，静态库使用库目录（通常是 `lib/` 或 `lib64/`），选定启动程序与运行时插件位于 `bin/`，许可证、README 和配置样例位于 `share/ark/`。安装仅使用显式公开头清单。Kernel 与可选插件原有的公开实现头、装配头继续保留；私有工厂头、解析头不进入安装包。关闭的运行时插件不会构建或安装，但公开 API target 仍可使用。

启用 Protobuf 时，Windows 还会把共享消息库 `AFProto.dll`（Debug 为 `AFProto_d.dll`）安装到 `bin/`，把导入库安装到库目录。消息 DLL 必须与宿主／插件及兼容的 Protobuf 运行时配置一起部署；原来链接静态 AFProto 的消费者需要重新构建。`ark::proto` target 名称保留，并提供生成消息的导入定义。非 Windows 构建继续使用静态 AFProto。参见[协议链接与迁移](../reference/protobuf_CN.md#构建与包含)。

arknet 仍是独立依赖：通过其自身构建单独安装兼容的 0.2.3 或更新版本包，并保持 Boost／Standalone Asio、SSL 设置与 ark 一致。ark 不把获取的或本地 arknet 源码复制到安装目录。消费者通过工具链或 `CMAKE_PREFIX_PATH` 提供其他依赖：core 使用 spdlog 1.17+、readerwriterqueue；构建了生成协议时还需要 Protobuf。header-only toml++ 作为私有实现编译进配置读取器；如果构建使用编译型 toml++ 库，安装后的读取器消费者会发现对应依赖包。可选驱动头与库仍需对应集成的依赖；安装公开头不代表这些集成已通过验证。分发依赖二进制时应保留对应许可证。

运行配置使用 TOML，Entity 定义与业务资源使用 JSON。`<ark/xml.hpp>` 包装器及 RapidXML 依赖已退役。迁移时须一起重建应用和插件，并安装到干净前缀；覆盖安装不会自动删除旧包遗留的头文件。

如需使用已经安装的 arknet，配置时添加 `-DARK_USE_INSTALLED_ARKNET=ON`。该选项优先于 `ARKNET_SOURCE_DIR` 与 `ARKNET_REVISION`；原有源码构建方式继续受支持。

包版本检查不能识别确切源码提交。要保留 ark 固定源码中的修复，请从同一提交构建 arknet，安装到干净前缀，并通过 `CMAKE_PREFIX_PATH` 选择该前缀。旧 release 的包可能满足 0.2.3 版本检查，却不包含后续修复；应单独确认其源码版本并重新构建使用方。

独立应用的 `CMakeLists.txt` 使用与源码消费相同的别名：

```cmake
find_package(ark CONFIG REQUIRED)
add_executable(game main.cpp)
target_link_libraries(game PRIVATE ark::core ark::mysql)
```

```sh
cmake -S game -B game-build -DCMAKE_PREFIX_PATH="/path/to/ark-prefix;/path/to/dependencies"
cmake --build game-build
```

使用 `<ark/plugin_manager.hpp>`、`<ark/mysql/mysql_module.hpp>` 等公开引用，不手工添加 ark 的源码或构建目录。源码消费时先调用 `add_subdirectory(path/to/ark ark-build)`，再链接相同 target。公开插件 API target 不负责加载运行时库。

仅生产插件时，设置 `ARK_BUILD_APPLICATIONS=OFF`、`ARK_BUILD_RUNTIME_PLUGINS=ON`，再通过 `ARK_BUILD_PLUGIN_<NAME>` 选择插件；构建和根安装均不包含启动器与七个服务动态库。参见 [MySQL／Redis 构建命令](../reference/build-options_CN.md#原生数据库插件)。AFProto 与选中插件保留原有依赖；仅运行时插件模式仍要求 Boost，MySQL 仍要求 SSL。安装与 CPack 按实际构建的 target 收集产物，公开 API／头文件布局不变。构建或安装动态库不代表数据库服务验收。

仅消费配置读取器时，调用 `find_package(ark CONFIG REQUIRED COMPONENTS config)` 并链接 `ark::config`，不会发现 core 或 arknet。使用 `COMPONENTS config_headers` 并链接 `ark::config_headers`，可仅获得 `<ark/application_manifest.hpp>` 声明；`ARK_BUILD_CONFIG=OFF` 时也可使用。请求不存在的必需组件会使包发现失败。默认发现导入 core、公开插件 target，以及已经构建的配置读取器和协议 target。

移动安装前缀后无需修改包文件；依赖前缀仍须可发现且 ABI 兼容。`cmake --build app-build --target check-install` 会暂存并迁移安装目录，验证独立安装消费者。以下隔离检查在临时副本中构建，删除临时生产者源码与构建目录后，再编译运行迁移后的消费者：

```sh
cmake -DARK_SOURCE_DIR="$PWD" -DARK_TEST_WORK=/tmp/ark-install-check \
  '-DARK_TEST_PREFIX_PATH=/path/to/installed/dependencies' \
  -P tests/install/run_isolated.cmake
```

工作目录必须尚不存在。使用新目录并添加 `-DARK_TEST_BUILD_CONFIG=OFF`，可检查声明与 core 包；依赖为启用 SSL 的 arknet 时，添加 `-DARK_TEST_MYSQL=ON` 可编译公开 MySQL 接口，但不运行数据库服务。需要工具链时，通过 `ARK_TEST_TOOLCHAIN` 同时传给生产者与消费者。

HTTP 消费者另有显式启用的安装运行时入口：

```sh
cmake -S tests/consumer/http -B http-installed-build \
  -DARK_TEST_HTTP_INSTALLED=ON -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_PREFIX_PATH=/path/to/relocated-prefix;/path/to/dependencies' \
  -DARK_TEST_HTTP_PLUGIN_DIR=/path/to/relocated-prefix/bin
cmake --build http-installed-build --parallel 2 --target check-http-installed
```

| 入口 | 覆盖范围与前提 |
| --- | --- |
| `tests/consumer/http` 的 `check-sdk` | 保留原有 build-tree 公开头消费检查；安装运行时模式默认关闭。 |
| `check-http-installed` | 仅原生 UNIX；通过 `find_package(ark)` 和已安装 HTTP DSO，检查 `dladdr` 实现路径、一次 HTTP 200、路由捕获销毁、端点关闭及重复停止。进程总期限为 15 秒，超时退出码为 124；3 秒接收与 5 秒清理观察仍分别保留。需要回环网络权限和启用 `ARK_BUILD_PLUGIN_HTTP=ON` 构建的 ABI 兼容安装；Debug／Release 配置及依赖前缀应与生产者一致。本地已验收 macOS Debug；没有 Windows 运行时入口。 |
| `tests/install/check_install.cmake` 的 `ARK_TEST_HTTP=ON` | 暂存并搬迁安装后加入同一个 HTTP 消费者。设置 `ARK_TEST_CONFIG=Debug` 或 `Release`；`ARK_TEST_HTTP_BINDIR` 默认 `bin`，可指定生产者自定义的相对二进制目录。需要已启用 HTTP 运行时的应用构建；core-only 的 `run_isolated.cmake` 生产者不构建该 DSO。 |

安装并搬迁前缀后，可通过数据库入口检查生产 MySQL、Redis 插件，无需连接数据库：

```sh
cmake -S tests/consumer/database -B database-installed-build \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_PREFIX_PATH=/path/to/relocated-prefix;/path/to/dependencies' \
  -DARK_TEST_DATABASE_PLUGIN_DIR=/path/to/relocated-prefix/bin
cmake --build database-installed-build --parallel 2 --target check-database-installed
```

| 入口 | 覆盖范围与前提 |
| --- | --- |
| `check-database-installed` | 通过 `find_package(ark)`、`ark::mysql`／`ark::redis`、公开头和生产 `ark_mysql_plugin`／`ark_redis_plugin` DSO，检查模块查找、未配置连接的初始生命周期、资源停止、模块移除、插件成功卸载及重复停止。CMake 运行器将整个进程限制为 15 秒，协作式清理观察为 5 秒。原生 UNIX 额外用 `dladdr` 核对各实现路径，其他平台排除此探针。本地仅验收 macOS Debug；不代表 Windows／Linux 运行时或操作系统物理移除映像已验收。 |
| 安装前提 | 启用 `ARK_BUILD_RUNTIME_PLUGINS`、`ARK_BUILD_PLUGIN_MYSQL` 和 `ARK_BUILD_PLUGIN_REDIS`，构建并安装选定产物，依赖启用 SSL 的 arknet；应用可保持关闭。Debug／Release 配置与 ABI 兼容的依赖前缀应匹配生产者。此检查不配置连接，不执行查询或命令，不替代真实服务与协议测试。 |
| `tests/install/check_install.cmake` 的 `ARK_TEST_DATABASE=ON` | 安装、前缀搬迁与导出路径审计后加入同一个消费者。设置 `ARK_TEST_CONFIG=Debug` 或 `Release`；`ARK_TEST_DATABASE_BINDIR` 默认 `bin`，支持自定义相对二进制目录。core-only 的 `run_isolated.cmake` 生产者不构建这些运行时插件。原有 `ARK_TEST_MYSQL` 模式仍只检查接口编译。 |

网络插件组合另有独立安装消费者。生产者设置
`ARK_BUILD_APPLICATIONS=OFF`、`ARK_BUILD_RUNTIME_PLUGINS=ON`，启用 Kernel、Net、
Bus、Utility 插件选项，再通过上面的根 `cmake --install` 命令安装。可以先整体搬迁
安装前缀，再配置消费者：

```sh
cmake -S tests/consumer/network -B network-installed-build \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_PREFIX_PATH=/path/to/relocated-prefix;/path/to/dependencies' \
  -DARK_TEST_NETWORK_PLUGIN_DIR=/path/to/relocated-prefix/bin \
  -DARK_TEST_NETWORK_BUS_CONFIG="$PWD/config/guest_room.bus.toml" \
  -DARK_TEST_NETWORK_ENTITY_SCHEMA="$PWD/examples/guest_room/resource/meta/entity_schema.json"
cmake --build network-installed-build --parallel 2 --target check-network-installed
```

此原生 UNIX 入口使用 `find_package(ark)` 和公开头，通过 `dladdr` 核对四个实现
DSO 的路径，把 canonical 资源复制到私有工作目录，仅替换 Bus 回环端口。检查
Bus 选择、Utility GUID、Kernel 配置／修改及拥有自身数据的快照、一次在管理器线程
处理的 Net TCP 请求与 Bus 回包，以及模块移除、回调／端点销毁、已接收连接／监听器
关闭和重复停止。Debug／Release 配置与依赖前缀须匹配生产者。外层进程护栏为
15 秒，请求和协作清理观察各 5 秒，不修改生产超时。本地 macOS Debug 验收覆盖
迁移后的运行时组合；跨主机部署、其他平台和性能仍需分别验收。已有 build-tree
五插件组合及安装 HTTP 检查分别覆盖各自路径。

## 生成归档包

完成构建后，通过安装规则生成标准归档：

```sh
cpack --config app-build/CPackConfig.cmake -G TGZ -C Release
cpack --config app-build/CPackConfig.cmake -G ZIP -C Release
```

归档位于构建目录的 `packages/` 下，仅包含选定安装产物与样例，不包含源码仓库、构建缓存或运行时业务数据。外部运行时库、平台兼容性、应用清单、工作目录与资源仍需由部署方准备。归档不是无外部依赖的应用安装器；签名、系统安装器与发布属于独立操作。
