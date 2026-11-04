# Configuration tools

`ark_config_tool` is a C++20 offline exporter for Entity definitions and static business profiles. It reads UTF-8 CSV, XLSX or canonical JSON, and shares Kernel's JSON model and validator. See [JSON Entity schema and business data](../reference/kernel.md#json-entity-schema-and-business-data).

## Build the native exporter

The exporter is optional (`ARK_BUILD_CONFIG_TOOL=OFF` by default). Prepare OpenXLSX 0.5.1 and nlohmann/json 3.12.0; Windows also uses Boost.Nowide for UTF-8 arguments. Select the vcpkg `config-tool` manifest feature for these packages. The CMake option enables the target; the manifest feature selects dependencies. With packages discoverable through a vcpkg toolchain or `CMAKE_PREFIX_PATH`, an exporter-only build is:

```sh
cmake -S . -B build-config-tool -DARK_BUILD_CONFIG_TOOL=ON \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_TESTS=OFF -DARK_BUILD_CORE_TESTS=OFF -DARK_BUILD_CONFIG=OFF \
  -DARK_BUILD_EXAMPLES=OFF -DARK_BUILD_CONFIG_TOOL_TESTS=ON
cmake --build build-config-tool --target ark_config_tool check-config-tool
```

This build does not configure framework networking, Protobuf or runtime plugins. The executable is `build-config-tool/bin/ark_config_tool` (`ark_config_tool.exe` on Windows). Multi-configuration generators also need `--config Release` on the build command. `ARK_INSTALL=ON` installs the executable and its notices. The XLSX reader belongs to this optional offline tool; a server loading JSON does not require it.

An OpenXLSX installation may bundle pugixml/miniz or use separate packages. For the latter, keep their CMake package configurations discoverable as well; the exporter loads their dependency targets before OpenXLSX. The vcpkg feature supplies these dependencies automatically.

## Export and validate

From the repository root, use a new output directory:

```sh
build-config-tool/bin/ark_config_tool \
  --schema resources/entity_config/fields.csv \
  --data GuestPlayer=resources/entity_config/GuestPlayer.csv \
  --output /path/to/new-resource-bundle
build-config-tool/bin/ark_config_tool \
  --validate /path/to/new-resource-bundle/meta/entity_schema.json
```

| Input mode | Arguments |
| --- | --- |
| Field table and profiles | `--schema fields.csv --data GuestPlayer=GuestPlayer.csv --output NEW_DIRECTORY` |
| Combined XLSX workbook | `--workbook config.xlsx --output NEW_DIRECTORY` |
| Normalize existing JSON | `--input entity_schema.json --output NEW_DIRECTORY` |
| Validate without writing | `--validate entity_schema.json` |

`--data Class=FILE` may repeat for distinct classes. Schema/profile inputs accept `.csv` or `.xlsx`. A multi-sheet schema XLSX requires `--schema-sheet SHEET`; a separate profile XLSX must contain one worksheet. A combined workbook requires a sheet named `fields`; every other sheet is profile data for the class with that exact sheet name.

The bundle contains `meta/entity_schema.json`, `code/entity_fields.hpp`, `code/EntityFields.cs` and the ownership marker `.ark-entity-export`. `--no-code` omits both code files. The generated constants include field, table and container indices, for example `ark::generated::guest_player_fields::hp` and `Ark.Generated.GuestPlayerFields.Hp`; they do not contain profile values or a wire protocol. Binding names must convert to valid, collision-free ASCII identifiers; use `--no-code` for other names.

`check-config-tool` compiles the generated C++ header. With a .NET 8 SDK, the optional C# consumer can also be compiled and run:

```sh
dotnet run --project tests/config_tool/bindings.csproj \
  -p:GeneratedFields=/path/to/new-resource-bundle/code/EntityFields.cs
```

The C++ exporter itself does not require .NET.

Existing destinations are refused unless `--replace` is given and the directory has the exact ownership marker. Use a dedicated output directory: replacement replaces the whole bundle. The tool validates and renders before modifying the destination, using a temporary sibling directory. Exit codes: success `0`, invalid CLI usage `2`, input/export failure `1`.

## Field tables and business profiles

Use the [field table](https://github.com/OpenArkStudio/ark/blob/main/resources/entity_config/fields.csv) and [GuestPlayer profiles](https://github.com/OpenArkStudio/ark/blob/main/resources/entity_config/GuestPlayer.csv) as templates. Edit them in a spreadsheet and save UTF-8 CSV, or use the same layout in XLSX worksheets.

| Column | Meaning |
| --- | --- |
| `class` | Exact Entity class name |
| `index` | Explicit stable positive field/table/container index, never inferred from row order |
| `name` | Resource member name |
| `type` | Scalar type, `class`, `table` or `container` |
| `reference` | Target class for tables/containers; `|`-separated parents on a `class` row |
| `mask` | Optional `|`-separated masks: `sync_view`, `sync_self`, `real_time`, `save`, `sync_team`, `sync_guild`, `sync_map`, `log` |
| `description` | Optional description preserved in JSON |

The first four headers are required. A `class` declaration leaves `index` and `name` empty; classes with member rows may omit it. Header aliases `field_index`, `field_name`, `type_class` mean `index`, `name`, `reference`. Masks may instead be individual boolean columns; specifying a mask twice fails. Inheritance is flattened with stable indices; missing parents, cycles and conflicting members fail export.

Scalars support `bool`, `int32`, `uint32`, `int64`, `uint64`, `float`, `double`, `string`, `guid`; authoring aliases `int`/`object` mean `int32`/`guid`. Vector and wide-string primitives are rejected. Kernel base fields occupy indices 1..4; the example uses profile `id` at 99 and business fields at 100..108. Preserve indices when rearranging rows.

Profile headers must list every scalar field exactly once; each row supplies complete typed values and a positive, globally unique `uint32` `id`. Missing columns or row cells, invalid types/ranges, duplicate IDs and unknown classes fail. Blank numeric cells do not supply defaults; an empty string is valid for strings. CSV must include trailing empty cells: with headers `id,label`, use `1,` or `1,""` for an empty label; `1` is incomplete and fails. Profiles initialize scalars, not table/container row defaults.

XLSX formulas are rejected even when cached results exist. Export calculated values or CSV instead. Integer values outside `[-9007199254740991, 9007199254740991]` must use XLSX **text** cells, especially `uint64` values such as `18446744073709551615`; spreadsheet numeric cells can already have lost precision. Strings and GUIDs also require text cells. CSV preserves decimal integer text, but cannot recover an already rounded spreadsheet value.

Copy `meta/entity_schema.json` into runtime `resource/meta/`. Kernel requires this JSON file: missing or malformed JSON fails startup, without XML fallback. Entity creation initializes schema zero/lazy values, copies the selected static profile, then applies explicit arguments field by field; arguments including zero override only the supplied fields. The tool does not reload running servers, migrate databases or filter separate client/server distributions.

## Source workbooks and retained tools

`resources/excel/` retains the original source workbooks. Their historical row
layout is not the native field-table layout above; translate the authoring data
and choose stable indices before exporting it with `ark_config_tool`.
The former Python/XML exporter, `gen-config.*` wrappers, GCM configuration-only
folder and unmaintained Photon XML generator/binaries are removed. Old
`resources/server/` XML exports are not runtime inputs. No tool overwrites
`src/core/meta_define.hpp` as part of this JSON export workflow.

| Path | Current scope |
| --- | --- |
| `tools/entity_config/` | Supported native CSV/XLSX/JSON exporter described above |
| `tools/plugin_and_module_tool/` | Historical scaffolding scripts/templates and an x86-64 macOS binary using older interfaces; no current SDK compatibility is established |
| `tools/gen-plugin-and-module.bat` | Retained wrapper for that historical scaffolding |
| `resources/photon/proto/` | Retained Protobuf schema and SQL outputs; the removed XML generator is not needed to read these source files |

For new code, follow [Create a module](modules.md) and [Create a plugin](plugins.md)
instead of generating from the historical scaffolding. Tool sources are under
[tools](https://github.com/OpenArkStudio/ark/tree/main/tools).
Application/plugin settings and logging use TOML; Kernel definitions and business
profiles use the exported JSON bundle. These inputs have separate responsibilities.
