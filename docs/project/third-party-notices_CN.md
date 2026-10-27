# 第三方声明

ark 采用 [Apache-2.0](https://github.com/OpenArkStudio/ark/blob/main/LICENSE) 许可证。各依赖的许可证保留在其源码或分发包中。独立的 arknet 网络库采用 BSL-1.0，其依赖见[第三方声明](https://openarkstudio.github.io/arknet/zh/project/third-party-notices/)。

文档使用 [Cloudflare Nimbus](https://github.com/cloudflare/nimbus) 0.17.0 与 0.7.11 starter，MIT 许可证保留在 [docs/site/LICENSE](https://github.com/OpenArkStudio/ark/blob/main/docs/site/LICENSE)。Astro、Tailwind、Pagefind 与固定版本的 npm 包保留各自适用的许可证。这些依赖只用于构建文档。

## 原生 Entity 配置导出器

可选的 `ark_config_tool` 使用以下依赖。完整许可证文本保存在
`docs/licenses/config-tool/`，分发可执行文件时应保留适用的声明。
框架运行时加载生成的 JSON 不需要 OpenXLSX。

| 依赖 | 用途 | 许可证文本 |
| --- | --- | --- |
| OpenXLSX 0.5.1 | 读取 XLSX | [BSD-3-Clause](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/OpenXLSX-LICENSE.md) |
| miniz 3.0.2 | OpenXLSX 的 ZIP 依赖 | [MIT](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/miniz-LICENSE.txt) |
| pugixml 1.15 | OpenXLSX 的 XML 依赖 | [MIT](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/pugixml-LICENSE.md) |
| nlohmann/json 3.12.0 | 共享 Entity JSON 解析与序列化 | [MIT](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/nlohmann-json-LICENSE.txt) |
| Boost.Nowide | Windows UTF-8 路径与参数 | [Boost Software License 1.0](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/Boost-LICENSE.txt) |

OpenXLSX 及其依赖文本来自 0.5.1 源码依赖集，nlohmann/json 文本来自 3.12.0
源码包，Boost 文本来自[官方许可证](https://www.boost.org/LICENSE_1_0.txt)。
软件包可能附带对应构建的额外声明；分发不同版本或链接组件时也应保留这些声明。
旧版 Python/openpyxl 流程有自己的软件包依赖，不属于原生可执行文件。

Inter 与 JetBrains Mono 使用 SIL Open Font License 1.1。站点保留 [Inter 许可证](/fonts/OFL-Inter.txt)与 [JetBrains Mono 许可证](/fonts/OFL-JetBrains-Mono.txt)，其中也包含社交分享卡片使用的内置 Inter 字体。
