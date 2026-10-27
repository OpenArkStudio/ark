# 配置工具

`ark_config_tool` 是 C++20 离线导出器，用于 Entity 定义与静态业务配置。它读取 UTF-8 CSV、XLSX 或规范 JSON，与 Kernel 共用 JSON 模型和校验器。格式见 [JSON 实体定义与业务配置](../reference/kernel_CN.md#json-实体定义与业务配置)。

## 构建原生导出器

导出器是可选目标，`ARK_BUILD_CONFIG_TOOL` 默认 `OFF`。准备 OpenXLSX 0.5.1 和 nlohmann/json 3.12.0；Windows 还使用 Boost.Nowide 处理 UTF-8 参数。使用 vcpkg 时选择 `config-tool` manifest feature。CMake 选项启用目标，manifest feature 选择依赖，两者需要分别设置。通过 vcpkg toolchain 或 `CMAKE_PREFIX_PATH` 提供依赖后，可以单独构建工具：

```sh
cmake -S . -B build-config-tool -DARK_BUILD_CONFIG_TOOL=ON \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_TESTS=OFF -DARK_BUILD_CORE_TESTS=OFF -DARK_BUILD_CONFIG=OFF \
  -DARK_BUILD_EXAMPLES=OFF -DARK_BUILD_CONFIG_TOOL_TESTS=ON
cmake --build build-config-tool --target ark_config_tool check-config-tool
```

这种构建不配置框架网络、Protobuf 或运行时插件。输出为 `build-config-tool/bin/ark_config_tool`，Windows 为 `ark_config_tool.exe`。多配置生成器需要在构建命令中添加 `--config Release`。`ARK_INSTALL=ON` 安装可执行文件及许可证声明。XLSX 读取器只属于这个可选离线工具；服务器加载 JSON 不需要它。

## 导出与校验

在仓库根目录运行，使用尚不存在的输出目录：

```sh
build-config-tool/bin/ark_config_tool \
  --schema resources/entity_config/fields.csv \
  --data GuestPlayer=resources/entity_config/GuestPlayer.csv \
  --output /path/to/new-resource-bundle
build-config-tool/bin/ark_config_tool \
  --validate /path/to/new-resource-bundle/meta/entity_schema.json
```

| 输入模式 | 参数 |
| --- | --- |
| 字段表与配置表 | `--schema fields.csv --data GuestPlayer=GuestPlayer.csv --output NEW_DIRECTORY` |
| 合并 XLSX 工作簿 | `--workbook config.xlsx --output NEW_DIRECTORY` |
| 规范化已有 JSON | `--input entity_schema.json --output NEW_DIRECTORY` |
| 只校验，不写文件 | `--validate entity_schema.json` |

`--data Class=FILE` 可以重复使用，但每个类只能指定一次。字段表与配置表支持 `.csv`、`.xlsx`。多工作表的字段 XLSX 需要 `--schema-sheet SHEET`；独立配置 XLSX 必须只有一张工作表。合并工作簿必须包含名为 `fields` 的工作表，其余工作表按名称精确对应类的配置数据。

输出包含 `meta/entity_schema.json`、`code/entity_fields.hpp`、`code/EntityFields.cs` 和所有权标记 `.ark-entity-export`。`--no-code` 不生成两个代码文件。生成常量包括字段、表和容器索引，例如 `ark::generated::guest_player_fields::hp` 与 `Ark.Generated.GuestPlayerFields.Hp`，不包含配置值或网络协议。名称转换后必须是合法且不冲突的 ASCII 标识符；其他名称可用 `--no-code` 仅导出 JSON。

`check-config-tool` 会编译生成的 C++ 头文件。准备 .NET 8 SDK 后，还可编译并运行可选的 C# 消费测试：

```sh
dotnet run --project tests/config_tool/bindings.csproj \
  -p:GeneratedFields=/path/to/new-resource-bundle/code/EntityFields.cs
```

C++ 导出器本身不需要 .NET。

工具默认拒绝已有目录；只有显式传入 `--replace` 且目录带有准确的所有权标记时才会替换。请使用专用输出目录：替换会覆盖整个目录。工具先校验并生成全部内容，再通过同级临时目录修改输出。退出码：成功为 `0`，CLI 用法错误为 `2`，输入或导出失败为 `1`。

## 字段表与业务配置

以仓库中的[字段表](https://github.com/OpenArkStudio/ark/blob/main/resources/entity_config/fields.csv)和 [GuestPlayer 配置表](https://github.com/OpenArkStudio/ark/blob/main/resources/entity_config/GuestPlayer.csv)为模板。在电子表格软件中编辑后保存为 UTF-8 CSV，或按相同布局制作 XLSX 工作表。

| 列 | 含义 |
| --- | --- |
| `class` | 精确匹配的 Entity 类名 |
| `index` | 显式、稳定的正数字段／表／容器索引，不按行序推断 |
| `name` | 资源成员名称 |
| `type` | 标量类型、`class`、`table` 或 `container` |
| `reference` | 表／容器引用的类；`class` 行可使用 `|` 分隔父类 |
| `mask` | 可选，以 `|` 分隔：`sync_view`、`sync_self`、`real_time`、`save`、`sync_team`、`sync_guild`、`sync_map`、`log` |
| `description` | 可选说明，保留到 JSON |

前四个列名必填。`class` 声明行的 `index`、`name` 留空；有成员行的类可以省略声明行。列别名 `field_index`、`field_name`、`type_class` 分别对应 `index`、`name`、`reference`。也可将 mask 作为独立布尔列，同一 mask 重复指定会报错。继承展开后保留索引；父类不存在、继承循环或成员冲突都会导致导出失败。

标量支持 `bool`、`int32`、`uint32`、`int64`、`uint64`、`float`、`double`、`string`、`guid`；制作表格时也可用 `int`／`object` 作为 `int32`／`guid` 的别名。向量和宽字符串原语会被拒绝。Kernel 基础字段占用索引 1..4，示例把配置 `id` 放在 99，业务字段放在 100..108。调整行序时应保留索引。

配置表首行必须完整且只出现一次地列出全部标量字段；每行提供完整的类型化值，以及正数、全局唯一的 `uint32` `id`。缺列、类型／范围非法、重复 ID 或未知类都会报错。数值空单元格不产生默认值，字符串可以是空字符串。配置初始化标量，不提供表／容器的默认行数据。

XLSX 公式会被拒绝，已有缓存结果也不例外。请导出计算后的值或使用 CSV。超出 `[-9007199254740991, 9007199254740991]` 的整数必须使用 XLSX **文本**单元格，尤其是 `18446744073709551615` 等 `uint64` 值；电子表格的数值单元格可能已丢失精度。字符串和 GUID 也需要文本单元格。CSV 可以保留十进制整数文本，但无法恢复已经舍入的电子表格值。

把 `meta/entity_schema.json` 放到运行目录的 `resource/meta/`。文件存在时它是权威资源，选中的 JSON 非法会使启动失败，不会回退 XML；文件不存在时仍保留旧 XML 路径。创建 Entity 时先完成 schema 零值／延迟初始化，再复制选中的静态配置，最后按字段应用显式参数；包括零值在内的参数只覆盖传入的字段。工具不负责热更新服务器、迁移数据库或按客户端／服务端过滤分发字段。

## 旧版 Python/XML 工作流

`tools/config_tool/config_tool.py` 与 `tools/gen-config.*` 为仍使用旧 XML 的应用保留原有 XLSX 到 XML 流程。它们使用 `resources/excel/`，不读取原生字段表布局，也不生成 `entity_schema.json`。以下命令与布局仅适用于这套旧流程。


使用 Python 3 运行旧生成器，并在所选 Python 环境中安装 `openpyxl`。只有打包独立可执行文件时才需要 PyInstaller。

```sh
python3 tools/config_tool/config_tool.py --help
python3 tools/config_tool/config_tool.py --path /path/to/resource
```

| 参数 | 必填 | 含义 |
| --- | --- | --- |
| `--path` / `-p` | 是 | 包含 `excel/`、`meta/`、`server/`、`code/` 的资源根目录 |
| `--help` / `-h` | 否 | 输出参数说明 |

参数帮助虽然写着「excel file path」，实际应传入**资源根目录**，不是 `.xlsx` 文件或 `excel/` 子目录。生成器在该根目录下写文件，并要求输出目录已经存在。

## 在临时资源副本中导出

先在仓库根目录复制附带的资源目录，再修改工作簿。macOS/Linux：

```sh
resource_demo=$(mktemp -d)
cp -R resources/. "$resource_demo/"
python3 tools/config_tool/config_tool.py --path "$resource_demo"
printf '%s\n' "$resource_demo"
```

Windows PowerShell，需先准备 Python 和 openpyxl：

```powershell
$resourceDemo = Join-Path ([System.IO.Path]::GetTempPath()) ("ark-resource-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $resourceDemo | Out-Null
Copy-Item resources/* $resourceDemo -Recurse -Force
python tools/config_tool/config_tool.py --path $resourceDemo
Write-Output $resourceDemo
```

附带的工作簿会在副本中生成以下文件：

| 输出 | 用途 |
| --- | --- |
| `meta/entity_class.config`、`meta/common.meta`、`meta/player.meta` | 实体元数据索引和定义 |
| `meta/config_class.config`、`meta/item.meta` 等配置 `.meta` 文件 | 配置元数据索引和字段定义 |
| `server/item.config`、`server/attribute.config`、`server/language.config`、`server/map.config` | 配置数据行 |
| `code/meta_define.hpp` | 生成的 C++ 名称和字段索引 |
| `code/AFMetaDefine.cs` | 生成的 C# 定义 |

部署前先检查生成的 XML 和定义。当前生成器不会另外导出一套客户端资源，也不会迁移数据库结构或重新加载运行中的进程。Kernel 加载这些数据时，需要把所需的 `meta/`、`server/` 输出放到运行目录的 `resource/` 下，详见[应用启动器](applications_CN.md#确定配置路径)。

## 修改普通配置工作簿

使用 `.xlsx` 格式。对于 `item.xlsx` 等普通配置，生成器读取第一张工作表，每列表示一个字段：

| 行 | 含义 | `item.xlsx` 第一列 |
| --- | --- | --- |
| 1 | 字段名 | `id` |
| 2 | 字段类型 | `uint64` |
| 3 | 目标标签 | `a` |
| 4 | 引用 | 空 |
| 5 | 说明 | `id` |
| 第 6 行起 | 数据值 | `100001`、`100002` 等 |

`entity_class.xlsx` 使用另一种布局描述实体类和字段，应以仓库附带工作簿为模板。修改 C++ 标识符时，不要顺带改变类名、字段名和反射字符串，它们属于资源约定。

<Aside type="caution" title="检查生成的资源范围">

现有导出器不提供完整的工作簿校验。不要依赖 `target` 标签隔离服务端和客户端字段，也不要假设空单元格会转换成应用默认值。部署前应检查输出 XML、字段类型和预期分发范围。

</Aside>

## 更新仓库中的生成头文件

资源变更经过检查后，在工作分支运行包装脚本。脚本导出仓库的 `resources/`，按项目 clang-format 规则格式化 `code/meta_define.hpp`，再复制到 `src/core/meta_define.hpp`，会覆盖这些生成文件。

macOS/Linux 上选择 Python 环境和项目支持的 clang-format 版本：

```sh
PYTHON=python3 CLANG_FORMAT=clang-format sh tools/gen-config.sh
```

Windows 按需设置 `PYTHON`、`CLANG_FORMAT` 后运行 `tools/gen-config.bat`。直接运行 `config_tool.py` **不会**执行最后的头文件复制。

生成头文件使用 snake_case 的 C++ 名称，保留 `Player` 等资源字符串：

```cpp
#include "ark/meta_define.hpp"

const auto& class_name = ark::entity_meta_player::self_name();
const auto field_index = ark::meta_name_index::get_index(class_name, "map_id");
```

类或字段不存在时返回索引 `0`。C# 输出保留原有命名规范。资源命名和格式变化需要单独确定兼容规则，生成的 C++ 名称变化不代表资源 schema 已迁移。

## 工具目录中的其他内容

| 目录 | 当前用途 |
| --- | --- |
| `entity_config` | 原生 Entity JSON 导出器 |
| `config_tool` | 旧版 Python/XML 导出器 |
| `plugin_and_module_tool` | 使用旧接口的历史脚手架模板，不应作为当前 SDK 示例；创建方式见[模块](modules_CN.md)和[插件](plugins_CN.md)指南 |
| `gcm` | 仅有配置示例，当前目录未提供可执行的环境管理工具 |

Photon 的历史协议资源保留在 `resources/photon/`，其中只有 x86-64 macOS/Linux 解析器二进制，仓库没有对应生成器源码；本次未验证其兼容性，也不将其列为当前可用工具。脚手架目录中的历史可执行文件同样未通过当前 SDK 验证。

工具源码位于 [tools](https://github.com/OpenArkStudio/ark/tree/main/tools)。应用的 TOML/XML 插件列表、日志配置与生成的 Kernel 资源是不同的配置输入。
