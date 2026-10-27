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

Profile headers must list every scalar field exactly once; each row supplies complete typed values and a positive, globally unique `uint32` `id`. Missing columns, invalid types/ranges, duplicate IDs and unknown classes fail. Blank numeric cells do not supply defaults; an empty string is valid for strings. Profiles initialize scalars, not table/container row defaults.

XLSX formulas are rejected even when cached results exist. Export calculated values or CSV instead. Integer values outside `[-9007199254740991, 9007199254740991]` must use XLSX **text** cells, especially `uint64` values such as `18446744073709551615`; spreadsheet numeric cells can already have lost precision. Strings and GUIDs also require text cells. CSV preserves decimal integer text, but cannot recover an already rounded spreadsheet value.

Copy `meta/entity_schema.json` into runtime `resource/meta/`. When present it is authoritative: malformed selected JSON fails startup, without falling back to XML. When absent, the legacy XML path remains. Entity creation initializes schema zero/lazy values, copies the selected static profile, then applies explicit arguments field by field; arguments including zero override only the supplied fields. The tool does not reload running servers, migrate databases or filter separate client/server distributions.

## Legacy Python/XML workflow

`tools/config_tool/config_tool.py` and `tools/gen-config.*` retain the old XLSX-to-XML workflow for applications still using legacy XML. They use `resources/excel/`, do not accept the native field-table layout and do not produce `entity_schema.json`. The following commands and layouts apply only to that legacy workflow.


Run the legacy generator with Python 3 and `openpyxl` installed in the selected Python environment. PyInstaller is needed only when packaging a standalone executable.

```sh
python3 tools/config_tool/config_tool.py --help
python3 tools/config_tool/config_tool.py --path /path/to/resource
```

| Argument | Required | Meaning |
| --- | --- | --- |
| `--path` / `-p` | Yes | Resource root containing `excel/`, `meta/`, `server/` and `code/` |
| `--help` / `-h` | No | Print usage |

Although the argument help says “excel file path”, pass the **resource root**, not an `.xlsx` file or its `excel/` subdirectory. The generator writes beneath that root and expects its output directories to exist.

## Export a disposable resource copy

From the repository root, copy the supplied resource tree before changing workbooks. On macOS/Linux:

```sh
resource_demo=$(mktemp -d)
cp -R resources/. "$resource_demo/"
python3 tools/config_tool/config_tool.py --path "$resource_demo"
printf '%s\n' "$resource_demo"
```

On Windows, in PowerShell with Python and openpyxl available:

```powershell
$resourceDemo = Join-Path ([System.IO.Path]::GetTempPath()) ("ark-resource-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $resourceDemo | Out-Null
Copy-Item resources/* $resourceDemo -Recurse -Force
python tools/config_tool/config_tool.py --path $resourceDemo
Write-Output $resourceDemo
```

The supplied workbooks produce these files inside the copy:

| Output | Used for |
| --- | --- |
| `meta/entity_class.config`, `meta/common.meta`, `meta/player.meta` | Entity metadata index and definitions |
| `meta/config_class.config`, `meta/item.meta` and other configuration `.meta` files | Configuration metadata index and field definitions |
| `server/item.config`, `server/attribute.config`, `server/language.config`, `server/map.config` | Configuration rows |
| `code/meta_define.hpp` | Generated C++ names and field indexes |
| `code/AFMetaDefine.cs` | Generated C# definitions |

Inspect the generated XML and definitions before staging them for an application. The current generator does not produce a separate client export, migrate a database schema or reload a running process. To load the generated data with Kernel, copy the required `meta/` and `server/` outputs under the runtime's `resource/` directory; see [Application launcher](applications.md#resolve-configuration-paths).

## Edit an ordinary configuration workbook

Use `.xlsx`. For ordinary workbooks such as `item.xlsx`, the generator reads the first worksheet, with one field per column:

| Row | Meaning | First column in `item.xlsx` |
| --- | --- | --- |
| 1 | Field name | `id` |
| 2 | Field type | `uint64` |
| 3 | Target label | `a` |
| 4 | Reference | Empty |
| 5 | Description | `id` |
| 6 onward | Data values | `100001`, `100002`, … |

`entity_class.xlsx` uses a different layout for entity classes and fields; use the supplied workbook as its template. Keep class names, field names and reflection strings stable when changing a C++ identifier: they are resource contracts.

<Aside type="caution" title="Review the generated resource set">

The existing exporter is not a complete workbook validator. Do not rely on its `target` labels to separate server-only and client-only fields, or on empty cells becoming application defaults. Check the emitted XML, field types and intended distribution before deployment.

</Aside>

## Update the checked-in generated header

After reviewing a resource change, run the wrapper on a working branch. It exports the repository's `resources/` tree, formats `code/meta_define.hpp` with the project's clang-format rules and copies it to `src/core/meta_define.hpp`. It overwrites those generated files.

On macOS/Linux, select the Python environment and the project's supported clang-format version:

```sh
PYTHON=python3 CLANG_FORMAT=clang-format sh tools/gen-config.sh
```

On Windows, set `PYTHON` and `CLANG_FORMAT` as needed, then run `tools/gen-config.bat`. Running `config_tool.py` directly does **not** perform the final header copy.

The generated header exposes snake_case C++ names while retaining resource strings such as `Player`:

```cpp
#include "ark/meta_define.hpp"

const auto& class_name = ark::entity_meta_player::self_name();
const auto field_index = ark::meta_name_index::get_index(class_name, "map_id");
```

An unknown class or field returns index `0`. C# output retains its existing naming conventions. Resource naming and format changes need separate compatibility decisions; generated C++ naming alone does not migrate the resource schema.

## Other files in the tools directory

| Directory | Current scope |
| --- | --- |
| `entity_config` | Native Entity JSON exporter |
| `config_tool` | Legacy Python/XML exporter |
| `plugin_and_module_tool` | Legacy scaffolding templates using older interfaces; do not use them as current SDK examples. Follow [Create a module](modules.md) and [Create a plugin](plugins.md) instead |
| `gcm` | Configuration examples only; the directory does not currently provide a runnable environment-management tool |

The retained Photon protocol assets in `resources/photon/` contain x86-64 macOS/Linux parser binaries without their generator source. Their compatibility was not verified, so they are not current supported tools. The historical scaffolding executable is likewise unverified against the current SDK.

The tool sources are under [tools](https://github.com/OpenArkStudio/ark/tree/main/tools). The application's TOML/XML plugin list, logger settings and generated Kernel resources are separate configuration inputs.
