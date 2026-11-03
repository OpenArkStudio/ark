# Offline tools

Usage and dependencies: [Configuration tools](../docs/guides/tools.md) · [配置工具](../docs/guides/tools_CN.md).

`entity_config/` contains the supported C++20 `ark_config_tool` exporter. It reads
explicit-index CSV/XLSX tables or canonical JSON and produces
`meta/entity_schema.json` with optional C++/C# field constants. Review and stage
the resulting bundle; it does not overwrite public framework headers.

The old Python/XML configuration exporter, `gen-config.*` wrappers and GCM-only
configuration folder are removed. Original `resources/excel/` workbooks are
retained as source data; their old layout must be adapted to the native format.

`plugin_and_module_tool/` and `gen-plugin-and-module.bat` retain historical
scaffolding. The scripts/templates target older interfaces and the supplied
macOS binary is x86-64; compatibility with the current SDK is not established.
Use the current module/plugin guides for new code.

原生 `entity_config/` 是当前配置导出入口；旧 Python/XML 配置流程与 GCM 配置目录已移除。
源工作簿继续保留，需整理为原生字段表格式。历史插件／模块脚手架不构成当前 SDK 使用示例。
