# 第三方声明

ark 使用 [Apache-2.0](https://github.com/OpenArkStudio/ark/blob/main/LICENSE)。运行时软件包见[依赖许可证](../reference/third-party-notices_CN.md)。

| 依赖 | 用途 | 许可证 |
| --- | --- | --- |
| arknet | 网络通信 | [BSL-1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE) · [依赖声明](https://openarkstudio.github.io/arknet/zh/project/third-party-notices/) |
| OpenXLSX 0.5.1 | 导入 XLSX | [BSD-3-Clause](https://github.com/troldal/OpenXLSX/blob/v0.5.1/LICENSE.md) |
| miniz 3.0.2 / 3.1.2 | XLSX 的 ZIP 依赖 | [MIT](https://github.com/richgel999/miniz/blob/3.1.2/LICENSE) |
| pugixml 1.15 / 1.16 | XLSX 的 XML 依赖 | [MIT](https://github.com/zeux/pugixml/blob/v1.16/LICENSE.md) |
| nlohmann/json 3.12.0 | Entity JSON 导入／导出 | [MIT](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT) |
| Boost.Nowide | Windows UTF-8 路径 | [BSL-1.0](https://www.boost.org/LICENSE_1_0.txt) |
| Cloudflare Nimbus | 文档站点 | [MIT](https://github.com/cloudflare/nimbus/blob/main/LICENSE) |
| Inter／JetBrains Mono | 文档与分享卡片字体 | [Inter OFL](/fonts/OFL-Inter.txt) · [JetBrains Mono OFL](/fonts/OFL-JetBrains-Mono.txt) |

导出器依赖按需启用，加载导出的 JSON 不需要 OpenXLSX。源码构建与固定 vcpkg 构建使用的 miniz／pugixml 版本不同；原文保留在 [docs/licenses/config-tool](https://github.com/OpenArkStudio/ark/tree/main/docs/licenses/config-tool)，分发时匹配实际软件包。其他文档构建依赖保留各自软件包中的许可证。
