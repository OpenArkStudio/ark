# Legacy XML configuration exporter

See [Configuration tools](../../docs/guides/tools.md) / [配置工具](../../docs/guides/tools_CN.md)
for the legacy Python/openpyxl requirements, workbook conventions, disposable exports and the
`tools/gen-config.*` wrappers. The exporter receives a resource root explicitly
with `--path`; it does not prepare a running application.

For new Entity JSON definitions and business profiles, use the native
[C++20 exporter](../entity_config/README.md). The old XLSX layout and generated
XML are separate from the native field-table and `entity_schema.json` format.

新 Entity JSON 定义与业务配置请使用[原生 C++20 导出器](../entity_config/README.md)。
本目录只保留旧版 Python/XLSX 到 XML 流程，不生成 `entity_schema.json`。
