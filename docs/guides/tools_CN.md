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

OpenXLSX 可以自带 pugixml/miniz，也可以使用独立安装的依赖。后一种情况需同时提供这些依赖的 CMake 包配置，导出器会先加载它们的依赖目标。vcpkg feature 会自动安装所需依赖。

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

把 `meta/entity_schema.json` 放到运行目录的 `resource/meta/`。Kernel 要求提供这份 JSON；文件缺失或内容非法都会使启动失败，不回退到 XML。创建 Entity 时先完成 schema 零值／延迟初始化，再复制选中的静态配置，最后按字段应用显式参数；包括零值在内的参数只覆盖传入的字段。工具不负责热更新服务器、迁移数据库或按客户端／服务端过滤分发字段。

## 源工作簿与保留工具

`resources/excel/` 保留原始源工作簿。其历史行布局不等同于上文原生字段表格式；
使用 `ark_config_tool` 前，需要整理制作数据并确定稳定索引。
原 Python/XML 导出器、`gen-config.*` 包装脚本、仅有配置的 GCM 目录，以及缺少维护源码的
Photon XML 生成脚本／二进制均已移除。`resources/server/` 中的旧 XML 导出物不是运行时输入。
这套 JSON 导出流程不会覆盖 `src/core/meta_define.hpp`。

| 路径 | 当前用途 |
| --- | --- |
| `tools/entity_config/` | 上文描述的原生 CSV／XLSX／JSON 导出器 |
| `tools/plugin_and_module_tool/` | 保留旧接口的历史脚本、模板和 x86-64 macOS 二进制，没有当前 SDK 兼容性证明 |
| `tools/gen-plugin-and-module.bat` | 该历史脚手架的包装脚本 |
| `resources/photon/proto/` | 保留 Protobuf schema 和 SQL 输出；读取这些源码不需要已删除的 XML 生成器 |

新代码应按[模块](modules_CN.md)和[插件](plugins_CN.md)指南编写，不使用历史脚手架生成。
工具源码位于 [tools](https://github.com/OpenArkStudio/ark/tree/main/tools)。
应用／插件参数和日志使用 TOML，Kernel 定义与业务配置使用导出的 JSON bundle，职责分别组织。
