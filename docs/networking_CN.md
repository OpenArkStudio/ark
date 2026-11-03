# 网络构建与回归测试

[English](networking.md) | 简体中文

ARK 使用 C++20，直接采用独立仓库的
[arknet](https://github.com/OpenArkStudio/arknet) 实现 TCP、WebSocket、HTTP
和 RDP。RDP 使用 RUDP 端点，保留旧 ARQ 报文格式，MTU 为 1400，默认关闭 FEC。
应用消息头格式和 Update 线程回调机制保持一致。本次不改造协程接口。

## 依赖

- CMake 3.21 及以上；GCC 12+、支持 C++20 的 Clang，或 VS 2022。
- 应用、官方运行时插件和网络测试使用 Boost 1.90+，依赖 Boost.Asio 和 Boost.Beast。
- 仅构建核心或原生数据库开发测试时，可以选择 Standalone Asio 1.38.0+。
- 固定 vcpkg 基线中的 RapidXML、spdlog/fmt 和 readerwriterqueue。
- 构建应用和插件时需要 Protobuf、Zstandard 和 zlib。

可选 feature 与许可声明见[依赖管理](dependencies_CN.md)。vcpkg 清单固定包版本基线。CMake 从 arknet 的独立仓库获取固定提交。
验证开发版本时，将 `ARKNET_SOURCE_DIR` 设为该仓库的绝对路径。

应用、官方运行时插件和当前网络测试保留 Boost.Asio／Boost.Beast 依赖。
如果同时设置 `ASIO_STANDALONE=ON` 与 `ARK_BUILD_APPLICATIONS=ON`、
`ARK_BUILD_RUNTIME_PLUGINS=ON` 或 `ARK_BUILD_NETWORK_TESTS=ON`，
CMake 会在获取依赖前明确报错，不会静默更换 Asio 依赖库。

Protobuf、Abseil 和 utf8_range 使用同一安装前缀。手工指定 `Protobuf_DIR` 时，
同时选择配套的 `absl_DIR` 和 `utf8_range_DIR`。

## 构建并运行网络测试

```sh
cmake -S . -B network-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_NETWORK_TESTS=ON \
  -DASIO_STANDALONE=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build network-build --parallel 2
cmake --build network-build --target check-network
```

默认使用 Boost。正式 C++ doctest 用例位于 `tests/network/`，也可以直接运行
测试程序，或用 `--test-case=RUDP*` 选择用例。不需要 Python 或 Ruby。

测试覆盖拆包、二进制数据、重连、连接上限、超时、Update 线程分发、回调中关闭连接、
HTTP 状态和请求数据、停机取消、回调内释放拥有者、非法输入，以及日志格式、目录创建和文件轮转。
CI 在 Linux、macOS、Windows 上检查 Boost 配置，并在 Linux 上运行内存、未定义行为和线程检查。
应用任务还会在启用 Protobuf 的配置下执行 `check-core`，包含内核初始化顺序回归。

2026-10-15 本机验证使用 Apple M4 Pro（14 核 CPU、48 GiB 内存）、macOS 27.0.1 arm64
和 Apple Clang 21.0.0。本机结果如下：

| 配置 | 核心用例／断言 | 网络用例／断言 |
| --- | ---: | ---: |
| Release、Boost 1.90、128 位 GUID、Protobuf、启用 TLS | 22／1,961 | 36／953 |
| Debug、Boost、128 位 GUID、ASan/UBSan，仅网络 | — | 36／953 |
| Release、Boost、ASan/UBSan，仅内核初始化 | 1／176 | — |
| Debug、Standalone Asio 1.38.2，仅核心、关闭 Protobuf/TLS | 14／1,734 | — |

Protobuf 用例包含正常消息、截断输入、Actor ID、含 NUL 的二进制消息、
旧版报文字节、对外 JSON 字段名，以及新的公开描述符和枚举名称。

核心总数包含内核与配置模块的四种初始化顺序组合，分别验证资源相对路径和带前导分隔符的旧路径。
完整 Release 应用、默认插件和示例已构建。
隔离进程加载 Bus、Net、Utility、Kernel 后进入主循环，收到 SIGINT 后正常退出。
HTTP 请求使用库拥有的 IO 池，网络用例覆盖未完成 future 和模块更新线程回调。

macOS ASan 未启用泄漏检测，Linux CI 启用 LeakSanitizer。这些结果验证正确性，不表示部署容量。
启用 TLS 的消费端构建尚无 ark 专用握手或证书用例；相关检查由 arknet 单独覆盖。

独立核心测试的构建、筛选方式见[核心回归测试](../tests/core/README_CN.md)。
启用 `ARK_BUILD_CORE_TESTS=ON` 时，`check-network` 同时执行核心用例。

## 构建应用

启用 `application` 清单特性以安装 Protobuf：

```sh
cmake -S . -B app-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  "-DVCPKG_MANIFEST_FEATURES=application;boost;etcd-client" \
  -DASIO_STANDALONE=OFF
cmake --build app-build --parallel 2
cmake --build app-build --target check-core
```

## Standalone Asio 构建

仅构建核心或可选原生数据库开发测试时，仍可使用 Standalone Asio。
请明确关闭应用、官方运行时插件和网络测试：

```sh
cmake -S . -B core-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON -DVCPKG_MANIFEST_FEATURES=config \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_RUNTIME_PLUGINS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_NETWORK_TESTS=OFF -DARK_BUILD_CORE_TESTS=ON \
  -DASIO_STANDALONE=ON
cmake --build core-build --target check-core
```

## 部署验收

本机回归结果不能作为 10–20 万 PCU 的容量结论。生产部署前仍需进行跨机器负载、
长时间运行和完整业务流程验收。ARQ/FEC 的性能数据见
[arknet 性能报告](https://openarkstudio.github.io/arknet/zh/performance/)。
