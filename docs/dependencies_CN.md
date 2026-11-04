# 依赖管理

[English](dependencies.md)

[vcpkg 清单](../vcpkg.json)固定统一的包注册表基线。CMake 通过导入 target 使用已安装的包，
仓库不再内置 `3rdparty/` 依赖源码，也不需要初始化依赖子模块。

| 构建范围 | vcpkg feature | 包与 CMake target |
| --- | --- | --- |
| 核心 | 始终安装 | `spdlog` → `spdlog::spdlog_header_only`；`readerwriterqueue` → `readerwriterqueue::readerwriterqueue` |
| Boost 网络 | `boost`（默认） | `boost-asio`、`boost-beast` |
| 应用清单读取 | `config`（默认） | `tomlplusplus` → `tomlplusplus::tomlplusplus` |
| 应用 | `application` | `args` → `taywee::args`、Protobuf、zlib、Zstandard |
| 压缩回归 | `compression-tests` | zlib、Zstandard、LZ4；`ARK_BUILD_COMPRESSION_TESTS=ON` |
| 已归档的 etcd 客户端插件 | `etcd-client` | `nlohmann-json` → `nlohmann_json::nlohmann_json` |
| 原生数据库开发 | `database` | OpenSSL |

spdlog 的导入 target 传递外部 fmt 依赖与编译定义。`ark::core` 的使用方会继承这两个包，
无需手工添加头文件路径或定义。只有启用应用或 etcd 插件时，CMake 才分别查找 args 和 JSON；
JSON 是 etcd 插件的私有依赖。

Consul、Etcd Client、Kafka 和 OSS SDK 已[归档](reference/directory-structure_CN.md#归档集成)，运行时插件默认关闭。应用构建使用
`-DVCPKG_MANIFEST_FEATURES="application;boost"`；保留通过 `-DARK_BUILD_PLUGIN_ETCD_CLIENT=ON` 和额外 `etcd-client` feature 显式选择归档 Etcd Client 的方式。
清单 feature 负责选择依赖包，`ARK_BUILD_*` 选项负责选择 CMake 构建目标，两者不会自动互相启用。

只构建不读取 TOML 配置的核心时，可以通过 `-DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON`
关闭默认 feature，设置 `-DARK_BUILD_CONFIG=OFF`，再按需选择网络 feature。
使用 Standalone Asio 且保留配置读取时，在 `-DASIO_STANDALONE=ON` 的同时显式设置
`-DVCPKG_MANIFEST_FEATURES=config`。

清单读取器优先使用已安装的 toml++ 3.4+ 包。未安装该包时，保留原有固定提交
`30172438cee64926dc41fdd9c11fb3ba5b2ba9de` 的获取路径；仅在找不到包时使用该路径，
正常的 vcpkg 配置构建不会安装第二份 toml++。
根 CMake 文件与数据库 CI 将 arknet 固定到不可变[源码提交 `d8ff34162cd1b490683f579ef23431068d8b7d8b`](https://github.com/OpenArkStudio/arknet/commit/d8ff34162cd1b490683f579ef23431068d8b7d8b)。该源码在 FEC 分片暂存抛出异常时释放受影响的未完成恢复组；应用交付回调抛出异常时保留已接受状态。Redis 拒绝非法小数 double 回复与嵌套 push 帧。使用方需要重新构建才能采用这些修复。

这些源码修复位于已发布的 [0.2.3 release](https://github.com/OpenArkStudio/arknet/releases/tag/v0.2.3) 之后，包版本仍为 0.2.3。安装包消费接受兼容的 arknet 0.2.3 或更新版本，但版本号本身不能识别这些修复。需要保留修复时，应从同一固定源码构建并安装；参见[安装包消费](get-started/installation_CN.md#安装并消费-cmake-包)。

本地开发可以通过 `ARKNET_SOURCE_DIR` 提供源码。已有构建目录会保留缓存中的 `ARKNET_REVISION`，升级时请按[安装说明](get-started/installation_CN.md#升级-arknet)显式更新缓存，或使用新的构建目录。既有性能与数据库报告仍对应各自列出的测试版本。

## Windows Debug 压缩依赖

LZ4 finder 支持 `lz4` 和 `lz4d` 名称，先检查每个已配置目录中的候选名称，再移到下一个目录。
这保留工具链的配置优先级：Debug 的 `debug/lib/lz4d.lib` 优先于后续 Release 目录中的
`lib/lz4.lib`；Release 仍优先查找自己的目录。Zstandard 查找方式和编译器设置保持不变。

Windows 压缩回归如果在 doctest 启动前以 `0xc0000135` 退出，应检查 `CMakeCache.txt` 中的
`LZ4_LIBRARY`，以及所选导入库依赖的 DLL。Debug 如果选中 Release LZ4 导入库，可能需要
配置的 `debug/bin` 运行目录之外的 DLL。更新 finder 后，使用
`cmake -U LZ4_LIBRARY -S . -B <build-dir>` 清除缓存选择，并重复原配置参数，或使用新的构建目录。
CI 运行时辅助脚本仍只导出匹配配置的目录，不向 Debug 运行路径混入 Release DLL。
finder fixture 只验证查找选择，不编译模拟库；Windows 实际运行仍需单独验收。

## 可选的旧版集成

physic 插件与 physics_scene 依赖 PhysX 3，当前构建没有已验证的 SDK 集成。
选择它们时会明确报错，不会静默替换成现代 PhysX 包。
旧版 simulate 服务器也缺少已验证的 PhysX 3/MessagePack 集成，继续保持禁用。
已从默认禁用的 http_server 删除未使用的 BeastHttp 头文件目录；这不代表该示例已经通过运行验收。

原 cpp-bredis 子模块没有活动引用，已移除；原生 Redis 客户端由 arknet 提供。
已移除内置源码中的许可声明保留在[第三方许可声明](reference/third-party-notices_CN.md)中，
安装的包也会提供各自的版权文件。

各平台命令见[安装指南](get-started/installation_CN.md)，回归入口见[网络构建](networking_CN.md)。
