# Business resources

`entity_config/` contains CSV templates for the native `ark_config_tool` exporter.
`excel/` preserves the original XLSX source workbooks; their historical layout
must be adapted to the native explicit-index schema/profile format.
`meta/`, `code/` and `client/` are output placeholders. `mysql/` retains database
schema SQL; `photon/proto/` retains the current Protobuf schema and SQL outputs.
See [configuration export](../docs/guides/tools.md) and
[layout and runtime staging](../docs/reference/directory-structure.md#runtime-resources-and-generated-data)
([中文](../docs/reference/directory-structure_CN.md#运行资源与生成数据)).

Generate a reviewed bundle in a dedicated output directory, then copy its
`meta/entity_schema.json` into runtime `resource/meta/`. The JSON-only Kernel
requires that file; missing or invalid JSON fails startup without XML fallback.
The old `server/` XML exports and Photon XML inputs/parser binaries are removed.
Source workbooks and all retained Proto/SQL files are unchanged by that removal.

`physic/` holds the TOML/JSON physics resources, while `script/` retains historical
Lua assets. These files do not establish a working PhysX or Lua integration.
External Photon, Realm and Terraria settings are mechanically migrated examples;
coordinate and validate their owning plugin parsers before using TOML.

原始 XLSX、Proto 和 SQL 继续保留；使用原生导出器生成并检查 JSON bundle，再部署
`resource/meta/entity_schema.json`。旧 XML 导出目录与 Photon XML 生成链路已移除。
