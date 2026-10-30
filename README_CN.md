<p align="right">简体中文 · <a href="README.md">English</a></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/OpenArkStudio/.github/main/assets/ark-logo-dark.svg?v=20261022">
    <img src="https://raw.githubusercontent.com/OpenArkStudio/.github/main/assets/ark-logo-light.svg?v=20261022" alt="ARK" width="337">
  </picture>
</p>

<h3 align="center">使用 C++20 构建模块化服务端应用的框架。</h3>

<p align="center">组织业务服务，描述实体数据，连接应用进程。</p>

<p align="center">
  <a href="https://github.com/OpenArkStudio/ark/actions/workflows/ci.yml"><img src="https://github.com/OpenArkStudio/ark/actions/workflows/ci.yml/badge.svg?event=merge_group" alt="合并队列 CI"></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/C%2B%2B-20-5267df" alt="C++20"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-5267df" alt="Apache-2.0"></a>
</p>

<p align="center"><a href="docs/get-started/installation_CN.md"><strong>开始使用 →</strong></a> &nbsp; · &nbsp; <a href="https://openarkstudio.github.io/ark/zh/">在线文档</a> &nbsp; · &nbsp; <a href="examples/">示例</a> &nbsp; · &nbsp; <a href="docs/project/roadmap_CN.md">路线图</a></p>

---

## 功能

- **业务服务**：插件、模块与生命周期回调，业务插件放在所属应用仓库。
- **实体数据**：属性、表格和容器，支持元数据、配置默认值与变更回调。
- **增量同步**：选定标量字段的基线／增量，以及拥有型表格／容器树基线与增量；[Godot 演示](https://github.com/OpenArkStudio/ark-client-godot)通过 Protobuf 同步移动与 HP／MP。
- **进程通信**：通过 [arknet](https://github.com/OpenArkStudio/arknet) 使用 TCP、WebSocket、HTTP 和 RUDP，提供总线标识与消息路由。
- **开发工具**：Protobuf 消息与原生 [CSV/XLSX 到 Entity JSON 导出器](docs/guides/tools_CN.md)，可生成 C++／C# 字段索引；Python/XML 导入器保留旧资源流程。

## 开始使用

支持 Linux、macOS 和 Windows，需要 **C++20**、**CMake 3.21+**、Ninja 和 vcpkg。Windows 需要 **Visual Studio 2022+**。

按[安装指南](docs/get-started/installation_CN.md)配置 `app-build/`，然后构建：

```sh
cmake --build app-build --config Release --parallel 2
```

按[快速开始](docs/get-started/quick-start_CN.md)准备运行所需的二进制文件和配置。在 macOS/Linux 上，从 `run/timers/` 启动 Timers：

```sh
./bin/app --busid=1.0.1.1 --name=timers \
  --app_conf=conf/timers.app.toml
```

日志与应用共用清单；省略设置时使用控制台 INFO 输出。详见[配置与迁移](docs/service-manifests_CN.md#内嵌日志配置)。

[Godot C# 演示客户端](https://github.com/OpenArkStudio/ark-client-godot)与 [`guest_room`](examples/guest_room/) 配套，展示移动同步、HP/MP 属性与断线重连。[原 Unity 客户端](https://github.com/OpenArkStudio/ArkClient-Unity3D)尚未与当前 ark 服务端完成兼容性验证。

[模块指南](docs/guides/modules_CN.md) · [架构](docs/concepts/architecture_CN.md) · [测试](docs/networking_CN.md) · [参与贡献](.github/CONTRIBUTING.md) · [Discord](https://discord.gg/GmyBbcv)

核心开发者：[Nick Yang](https://github.com/NickYang1988)、[flyicegood](https://github.com/flyicegood) 和 [loscaz](https://github.com/loscaz)。感谢所有[贡献者](https://github.com/OpenArkStudio/ark/graphs/contributors)。

---

[Apache-2.0](LICENSE) · 版权所有 [OpenArkStudio](https://github.com/OpenArkStudio)。
