# Native Entity configuration exporter / 原生 Entity 配置导出器

`ark_config_tool` is an optional C++20 offline tool. It converts explicit-index
UTF-8 CSV/XLSX field tables and complete business profiles into one validated
`meta/entity_schema.json`, plus optional `code/entity_fields.hpp` and
`code/EntityFields.cs`. It can also validate or normalize existing canonical JSON.

Build with `ARK_BUILD_CONFIG_TOOL=ON` and the vcpkg `config-tool` feature (or
equivalent OpenXLSX 0.5.1/nlohmann packages). Windows uses Boost.Nowide for UTF-8
arguments. The exporter-only configuration does not configure framework
networking or runtime plugins; the server does not need OpenXLSX to load JSON.

See [Configuration tools](../../docs/guides/tools.md) for build commands, CLI,
field-table layout, precision rules, overwrite protection and deployment.
Run `ark_config_tool --help` for the available input modes.

`ark_config_tool` 是可选的 C++20 离线工具。它将带显式稳定索引的 UTF-8 CSV/XLSX
字段表与完整业务配置导出为经过校验的 `meta/entity_schema.json`，并可生成 C++、C#
索引常量，也可校验或规范化已有 JSON。构建选项、命令、字段表布局、精度要求与部署方式见
[配置工具](../../docs/guides/tools_CN.md)。服务器加载 JSON 不需要 OpenXLSX。

The Python importer in `tools/config_tool` remains the legacy XML workflow.
`tools/config_tool` 中的 Python 导入器仅保留旧 XML 流程。
